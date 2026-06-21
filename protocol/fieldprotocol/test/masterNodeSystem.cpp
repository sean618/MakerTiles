/*
 * Copyright © 2024 - 2024 Sean Bremner. All rights reserved.>>
 */

#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <optional>

#include "../src/fpCommon.hpp"
#include "../src/fpDaemon.hpp"
#include "../src/fpMaster.hpp"
#include "../src/fpNode.hpp"
#include "networkSim.hpp"
#include "testCommon.hpp"

// ==================================== //
// USB

// Backing storage is 8 bytes to match the field's declared size below (the
// original declared a 1-byte variable for an 8-byte field, an out-of-bounds
// read that only happened to be harmless under the original memory layout).
uint64_t usbRxFullCount = 0;
fp::FieldEntry usbFields[] = {
    // ptr            ,   name         ,  span,  type                , size,   flags,     setFieldFn,  getFieldFn,  units
    {  &usbRxFullCount, "usb_rx_full",     1,  fp::FieldDataType::Uint,    8,     fp::FieldFlags::Gettable,        nullptr,        nullptr, ""},
};
fp::FieldTable usbTable = {
    .fields = usbFields,
    .numFields = sizeof(usbFields)/sizeof(fp::FieldEntry)
};

// ==================================== //
// Battery

typedef struct {
    bool standby;
    bool charging;
    uint32_t voltagemv;
    uint32_t currentma;
    uint32_t chargePercent;
} sBatteryBoardDriver;

sBatteryBoardDriver batteryDrv = {0};

fp::FieldEntry batteryFields[] = {
    {&batteryDrv.standby,       "charged",       1, fp::FieldDataType::Boolean,   1,  fp::FieldFlags::Gettable, nullptr, nullptr, ""},
    {&batteryDrv.charging,      "charging",      1, fp::FieldDataType::Boolean,   1,  fp::FieldFlags::Gettable, nullptr, nullptr, ""},
    {&batteryDrv.voltagemv,     "voltagemv",     1, fp::FieldDataType::Uint,      4,  fp::FieldFlags::Gettable, nullptr, nullptr, "mV"}, 
    {&batteryDrv.currentma,     "currentma",     1, fp::FieldDataType::Uint,      4,  fp::FieldFlags::Gettable, nullptr, nullptr, "mA"}, 
    {&batteryDrv.chargePercent, "level",         1, fp::FieldDataType::Uint,      4,  fp::FieldFlags::Gettable, nullptr, nullptr, "percent"},
};
fp::FieldTable batteryTable = {
    .fields = batteryFields,
    .numFields = sizeof(batteryFields)/sizeof(fp::FieldEntry)
};

// ==================================== //
// Buttons

constexpr uint8_t kNumButtons = 5;

typedef struct {
    uint8_t pressed[kNumButtons];
    uint32_t pressedCount[kNumButtons];
} sButtonBoardDriver;


sButtonBoardDriver buttonsDrv = {};

fp::FieldEntry buttonFields[] = {
    {&buttonsDrv.pressed[0],      "pressed",          kNumButtons, fp::FieldDataType::Boolean,   1,  fp::FieldFlags::Gettable, nullptr, nullptr, ""}, 
    {&buttonsDrv.pressedCount[0], "pressed_counts",   kNumButtons, fp::FieldDataType::Uint,      4,  fp::FieldFlags::Gettable, nullptr, nullptr, ""},
};
fp::FieldTable buttonTable = {
    .fields = buttonFields,
    .numFields = sizeof(buttonFields)/sizeof(fp::FieldEntry)
};

// ==================================== //
// DC motors

constexpr uint8_t kNumDcMotors = 2;

typedef struct sdcmotorBoardDriver {
    bool coastMode[kNumDcMotors]; // Coast when velocity is 0 (vs braking)
    uint32_t acceleration[kNumDcMotors]; // change in the percentage of max power per msec
    uint32_t deceleration[kNumDcMotors];
    int32_t desiredVoltagePercent[kNumDcMotors];
    bool brake[kNumDcMotors];
    uint32_t rpm[kNumDcMotors];
} dcmotorBoardDriver;

dcmotorBoardDriver dcmDrv = {};

fp::FieldEntry dcmotorsFields[] = {
    {&dcmDrv.desiredVoltagePercent[0],   "powers",          kNumDcMotors,  fp::FieldDataType::Int,      4,  fp::FieldFlags::Gettable | fp::FieldFlags::Settable, nullptr, nullptr, "percentage"},
    {&dcmDrv.brake[0],                   "braking",         kNumDcMotors,  fp::FieldDataType::Boolean,  1,  fp::FieldFlags::Gettable | fp::FieldFlags::Settable, nullptr, nullptr, ""},
    {&dcmDrv.acceleration[0],            "accelerations",   kNumDcMotors,  fp::FieldDataType::Uint,     4,  fp::FieldFlags::Gettable | fp::FieldFlags::Settable, nullptr, nullptr, ""},
    {&dcmDrv.deceleration[0],            "decelerations",   kNumDcMotors,  fp::FieldDataType::Uint,     4,  fp::FieldFlags::Gettable | fp::FieldFlags::Settable, nullptr, nullptr, ""},
    {&dcmDrv.coastMode[0],               "coast_mode",      kNumDcMotors,  fp::FieldDataType::Boolean,  1,  fp::FieldFlags::Gettable | fp::FieldFlags::Settable, nullptr, nullptr, ""},
    {&dcmDrv.rpm,                        "rpm",             kNumDcMotors,  fp::FieldDataType::Uint,     4,  fp::FieldFlags::Gettable           , nullptr, nullptr, ""},
};
fp::FieldTable dcmotorsTable = {
    .fields = dcmotorsFields,
    .numFields = sizeof(dcmotorsFields)/sizeof(fp::FieldEntry)
};

// ==================================== //
// Distance sensor

typedef struct sDistanceSensorDriver {
   uint32_t numObjects;
   int16_t distance[4];
} DistanceSensorDriver;

static DistanceSensorDriver distDrv = {};

fp::FieldEntry distanceSensorFields[] = {
   { &distDrv.numObjects, "num_objects",       1,  fp::FieldDataType::Uint,  1,  fp::FieldFlags::Gettable | fp::FieldFlags::Joined,  nullptr, nullptr, ""},
   { &distDrv.distance,   "distance",          4,  fp::FieldDataType::Int,   2,  fp::FieldFlags::Gettable | fp::FieldFlags::Joined,  nullptr, nullptr, ""},
};
fp::FieldTable distanceSensorTable = {
    .fields = distanceSensorFields,
    .numFields = sizeof(distanceSensorFields)/sizeof(fp::FieldEntry)
};

// ==================================== //
// IMU

typedef struct sIMUBoardDriver {
    float pitch;
    float roll;
    float yaw;
    float acceleration_x;
    float acceleration_y;
    float acceleration_z;
    float angular_velocity_x;
    float angular_velocity_y;
    float angular_velocity_z;
    float gravity_x;
    float gravity_y;
    float gravity_z;
} IMUBoardDriver;

IMUBoardDriver imu = {0};

//    ptr,                           name,            numFields,  data_type,       data_size,  gettable,  settable,  setfn,  getfn,  units
fp::FieldEntry imuFields[] = {
    { &imu.pitch,                   "pitch",                 1,  fp::FieldDataType::Float,  4,  fp::FieldFlags::Gettable, nullptr, nullptr, "degrees" }, //  p:  -180 -> 180 
    { &imu.roll,                    "roll",                  1,  fp::FieldDataType::Float,  4,  fp::FieldFlags::Gettable, nullptr, nullptr, "degrees" }, //  r:  -90 -> 90 
    { &imu.yaw,                     "yaw",                   1,  fp::FieldDataType::Float,  4,  fp::FieldFlags::Gettable, nullptr, nullptr, "degrees" }, //  h:  0 -> 360 
    { &imu.acceleration_x,          "acceleration_x",        1,  fp::FieldDataType::Float,  4,  fp::FieldFlags::Gettable, nullptr, nullptr, "m/s^2"   }, 
    { &imu.acceleration_y,          "acceleration_y",        1,  fp::FieldDataType::Float,  4,  fp::FieldFlags::Gettable, nullptr, nullptr, "m/s^2"   }, 
    { &imu.acceleration_z,          "acceleration_z",        1,  fp::FieldDataType::Float,  4,  fp::FieldFlags::Gettable, nullptr, nullptr, "m/s^2"   }, 
    { &imu.angular_velocity_x,      "angular_velocity_x",    1,  fp::FieldDataType::Float,  4,  fp::FieldFlags::Gettable, nullptr, nullptr, "m/s"     }, 
    { &imu.angular_velocity_y,      "angular_velocity_y",    1,  fp::FieldDataType::Float,  4,  fp::FieldFlags::Gettable, nullptr, nullptr, "m/s"     }, 
    { &imu.angular_velocity_z,      "angular_velocity_z",    1,  fp::FieldDataType::Float,  4,  fp::FieldFlags::Gettable, nullptr, nullptr, "m/s"     }, 
    { &imu.gravity_x,               "gravity_x",             1,  fp::FieldDataType::Float,  4,  fp::FieldFlags::Gettable, nullptr, nullptr, ""}, 
    { &imu.gravity_y,               "gravity_y",             1,  fp::FieldDataType::Float,  4,  fp::FieldFlags::Gettable, nullptr, nullptr, ""}, 
    { &imu.gravity_z,               "gravity_z",             1,  fp::FieldDataType::Float,  4,  fp::FieldFlags::Gettable, nullptr, nullptr, ""},
    // { &imu.temp,                          "chip_temperature",     IMU_FIELDS_OFFSET + 12,  1,  AF_FIELD_TYPE_FLOAT,  4,  0,  &temperatureMetaData,     nullptr, ""},
};
fp::FieldTable imuTable = {
    .fields = imuFields,
    .numFields = sizeof(imuFields)/sizeof(fp::FieldEntry)
};

// ==================================== //
// Joystick

typedef struct sJoystickDriver {
    uint16_t horizontal;
    uint16_t vertical;
} JoystickDriver;

static JoystickDriver joystick = {};

fp::FieldEntry joystickFields[] = {
    { &joystick.horizontal,   "horizontal",   1,  fp::FieldDataType::Uint,   2,  fp::FieldFlags::Gettable,  nullptr, nullptr, "degrees"},
    { &joystick.vertical,     "vertical",     1,  fp::FieldDataType::Uint,   2,  fp::FieldFlags::Gettable,  nullptr, nullptr, "degrees"},
};
fp::FieldTable joystickTable = {
    .fields = joystickFields,
    .numFields = sizeof(joystickFields)/sizeof(fp::FieldEntry)
};

// ==================================== //
// Led strip

constexpr uint16_t kLedStripMaxLeds = 350;
#define PIXEL_BUFFER_LENGTH (3 * kLedStripMaxLeds) // Limited by the amount of ram we have 

typedef struct {
	uint8_t pixelBuffer[PIXEL_BUFFER_LENGTH];
	uint8_t userBrightness;
	uint16_t refreshPeriodMs;
	uint16_t lastEstimatedCurrentMa;
	uint16_t maxCurrentMa;
	bool show;
} ledStripDriver;
ledStripDriver ledDrv = {};

bool setShowField(fp::FieldEntry * field, fp::FieldIndex fieldIndex, fp::FieldIndex numFields, uint8_t * data) {
	ledDrv.show = data[0];
    return true;
}

fp::FieldEntry ledStripFields[] = {
    // ptr                          ,  name             ,          span,                  type,                  size,   gettable,  settable,  setFieldFn,  getFieldFn,  units
	{ &ledDrv.userBrightness        , "brightness",                1,                  fp::FieldDataType::Uint,     1,      fp::FieldFlags::Gettable | fp::FieldFlags::Settable,      nullptr,  nullptr, ""},
	{ &ledDrv.refreshPeriodMs       , "refresh_period_ms",         1,                  fp::FieldDataType::Uint,     2,      fp::FieldFlags::Gettable | fp::FieldFlags::Settable,      nullptr,  nullptr, ""},
	{ &ledDrv.pixelBuffer[0]        , "colours",                   kLedStripMaxLeds,   fp::FieldDataType::Uint,     3,      fp::FieldFlags::Gettable | fp::FieldFlags::Settable,      nullptr,  nullptr, ""},
	{ nullptr                       , "show",                      1,                  fp::FieldDataType::Boolean,  1,      fp::FieldFlags::Settable,                  setShowField,  nullptr, ""},
	{ &ledDrv.lastEstimatedCurrentMa, "current",                   1,                  fp::FieldDataType::Uint,     2,      fp::FieldFlags::Gettable           ,                      nullptr,  nullptr, ""},
	{ &ledDrv.maxCurrentMa          , "max_current",               1,                  fp::FieldDataType::Uint,     2,      fp::FieldFlags::Gettable | fp::FieldFlags::Settable,      nullptr,  nullptr, ""},
};
fp::FieldTable ledStripTable = {
    .fields = ledStripFields,
    .numFields = sizeof(ledStripFields)/sizeof(fp::FieldEntry)
};

// ==================================== //
// Rotary encoder

typedef struct sRotaryEncoderDriver {
    int32_t position;
} RotaryEncoderDriver;

RotaryEncoderDriver rotaryDrv = {0};

fp::FieldEntry rotaryEncoderFields[] = {
    {&rotaryDrv.position,   "position",  1,  fp::FieldDataType::Int,      4,  fp::FieldFlags::Gettable, nullptr, nullptr, ""},
};
fp::FieldTable rotaryEncoderTable = {
    .fields = rotaryEncoderFields,
    .numFields = sizeof(rotaryEncoderFields)/sizeof(fp::FieldEntry)
};

// ==================================== //
// Screen

constexpr uint8_t kMaxPacketPixels = 80;
constexpr uint8_t kNumCharacters = 64;

typedef struct {
    uint16_t x;
    uint16_t y;
    uint16_t width;
    uint16_t height;
} tPixelWindow;

typedef struct {
    uint16_t x;
    uint16_t y;
    uint16_t width;
    uint16_t height;
    uint16_t colour;
} tDrawRectangle;

typedef struct {
    uint16_t x;
    uint16_t y;
    uint8_t size;
    uint16_t colour;
    uint16_t backgroundColour;
    uint16_t numChar;
} tDrawText;

typedef struct {
    uint16_t running;
    uint16_t rowPixelIndex;
} tPixelStream;

typedef struct {
    uint32_t cmdSpaceRemaining;
    uint8_t backlightEnabled;
    uint8_t invertColours;
    uint8_t rotation;
    uint8_t resolutionScaling;
    uint8_t newResolutionScaling;
    tDrawRectangle drawRectangle;
    char text[kNumCharacters];
    tDrawText drawText;
    tPixelWindow newPixelWindow;
    bool rectangleDraw;
    bool textDraw;
    bool start_pixel_streaming;
    uint16_t pixel_data_stream[kMaxPacketPixels];
    bool stop_pixel_streaming;
} tScreenBoardDriver;
tScreenBoardDriver screen =  {};

fp::FieldEntry screenFields[] = {
    { &screen.drawRectangle.x,               "rectangle_x",              1,                     fp::FieldDataType::Uint,      2,   fp::FieldFlags::Gettable | fp::FieldFlags::Settable, nullptr, nullptr, ""},
    { &screen.drawRectangle.y,               "rectangle_y",              1,                     fp::FieldDataType::Uint,      2,   fp::FieldFlags::Gettable | fp::FieldFlags::Settable, nullptr, nullptr, ""},
    { &screen.drawRectangle.width,           "rectangle_width",          1,                     fp::FieldDataType::Uint,      2,   fp::FieldFlags::Gettable | fp::FieldFlags::Settable, nullptr, nullptr, ""},
    { &screen.drawRectangle.height,          "rectangle_height",         1,                     fp::FieldDataType::Uint,      2,   fp::FieldFlags::Gettable | fp::FieldFlags::Settable, nullptr, nullptr, ""},
    { &screen.drawRectangle.colour,          "rectangle_colour",         1,                     fp::FieldDataType::Uint,      2,   fp::FieldFlags::Gettable | fp::FieldFlags::Settable, nullptr, nullptr, ""},
    { &screen.rectangleDraw,                 "rectangle_draw",           1,                     fp::FieldDataType::Boolean,   1,   fp::FieldFlags::Gettable | fp::FieldFlags::Settable, nullptr, nullptr, ""},
    { &screen.text[0],                       "text",                     kNumCharacters,        fp::FieldDataType::Utf8Char,  1,   fp::FieldFlags::Gettable | fp::FieldFlags::Settable, nullptr, nullptr, ""},
    { &screen.drawText.x,                    "text_x",                   1,                     fp::FieldDataType::Uint,      2,   fp::FieldFlags::Gettable | fp::FieldFlags::Settable, nullptr, nullptr, ""},
    { &screen.drawText.y,                    "text_y",                   1,                     fp::FieldDataType::Uint,      2,   fp::FieldFlags::Gettable | fp::FieldFlags::Settable, nullptr, nullptr, ""},
    { &screen.drawText.size,                 "text_size",                1,                     fp::FieldDataType::Uint,      1,   fp::FieldFlags::Gettable | fp::FieldFlags::Settable, nullptr, nullptr, ""},
    { &screen.drawText.colour,               "text_colour",              1,                     fp::FieldDataType::Uint,      2,   fp::FieldFlags::Gettable | fp::FieldFlags::Settable, nullptr, nullptr, ""},
    { &screen.drawText.backgroundColour,     "text_background_colour",   1,                     fp::FieldDataType::Uint,      2,   fp::FieldFlags::Gettable | fp::FieldFlags::Settable, nullptr, nullptr, ""},
    { &screen.textDraw,                      "text_draw",                1,                     fp::FieldDataType::Boolean,   1,   fp::FieldFlags::Gettable | fp::FieldFlags::Settable, nullptr, nullptr, ""},
    { &screen.newResolutionScaling,          "resolution_scaling",       1,                     fp::FieldDataType::Uint,      1,   fp::FieldFlags::Gettable | fp::FieldFlags::Settable, nullptr, nullptr, ""},
    { &screen.invertColours,                 "invert_colours",           1,                     fp::FieldDataType::Boolean,   1,   fp::FieldFlags::Gettable | fp::FieldFlags::Settable, nullptr, nullptr, ""},
    { &screen.rotation,                      "rotation",                 1,                     fp::FieldDataType::Uint,      1,   fp::FieldFlags::Gettable | fp::FieldFlags::Settable, nullptr, nullptr, ""},
    { &screen.cmdSpaceRemaining,             "cmd_space_remaining",      1,                     fp::FieldDataType::Uint,      4,   fp::FieldFlags::Gettable | fp::FieldFlags::Settable, nullptr, nullptr, ""},
    { &screen.newPixelWindow.x,              "pixel_window_x",           1,                     fp::FieldDataType::Uint,      2,   fp::FieldFlags::Gettable | fp::FieldFlags::Settable, nullptr, nullptr, ""},
    { &screen.newPixelWindow.y,              "pixel_window_y",           1,                     fp::FieldDataType::Uint,      2,   fp::FieldFlags::Gettable | fp::FieldFlags::Settable, nullptr, nullptr, ""},
    { &screen.newPixelWindow.width,          "pixel_window_width",       1,                     fp::FieldDataType::Uint,      2,   fp::FieldFlags::Gettable | fp::FieldFlags::Settable, nullptr, nullptr, ""},
    { &screen.newPixelWindow.height,         "pixel_window_height",      1,                     fp::FieldDataType::Uint,      2,   fp::FieldFlags::Gettable | fp::FieldFlags::Settable, nullptr, nullptr, ""},
    { &screen.start_pixel_streaming,         "start_pixel_streaming",    1,                     fp::FieldDataType::Boolean,   1,   fp::FieldFlags::Gettable | fp::FieldFlags::Settable, nullptr, nullptr, ""},
    { &screen.pixel_data_stream,             "pixel_data_stream",        kMaxPacketPixels,      fp::FieldDataType::Uint,      2,   fp::FieldFlags::Gettable | fp::FieldFlags::Settable, nullptr, nullptr, ""},
    { &screen.stop_pixel_streaming,          "stop_pixel_streaming",     1,                     fp::FieldDataType::Boolean,   1,   fp::FieldFlags::Gettable | fp::FieldFlags::Settable, nullptr, nullptr, ""},
};
fp::FieldTable screenTable = {
    .fields = screenFields,
    .numFields = sizeof(screenFields)/sizeof(fp::FieldEntry)
};

// ==================================== //
// Servos

constexpr uint8_t kNumServos = 2;
typedef struct sservoBoardDriver {
	uint16_t minPulseUsec[kNumServos];
	uint16_t maxPulseUsec[kNumServos];
    uint32_t angle[kNumServos];
} servoBoardDriver;

servoBoardDriver servoDrv1 = {};
fp::FieldEntry servosFields1[] = {
    {&servoDrv1.angle          , "angles",      kNumServos,  fp::FieldDataType::Uint,  4,  fp::FieldFlags::Gettable | fp::FieldFlags::Settable, nullptr, nullptr, "degrees"},
    {&servoDrv1.minPulseUsec[0], "min_pulses",  kNumServos,  fp::FieldDataType::Uint,  2,  fp::FieldFlags::Gettable | fp::FieldFlags::Settable, nullptr, nullptr, "micro-seconds"},
    {&servoDrv1.maxPulseUsec[0], "max_pulses",  kNumServos,  fp::FieldDataType::Uint,  2,  fp::FieldFlags::Gettable | fp::FieldFlags::Settable, nullptr, nullptr, "micro-seconds"},
};
fp::FieldTable servosTable1 = {
    .fields = servosFields1,
    .numFields = sizeof(servosFields1)/sizeof(fp::FieldEntry)
};

servoBoardDriver servoDrv2 = {};
fp::FieldEntry servosFields2[] = {
    {&servoDrv2.angle          , "angles",      kNumServos,  fp::FieldDataType::Uint,  4,  fp::FieldFlags::Gettable | fp::FieldFlags::Settable, nullptr, nullptr, "degrees"},
    {&servoDrv2.minPulseUsec[0], "min_pulses",  kNumServos,  fp::FieldDataType::Uint,  2,  fp::FieldFlags::Gettable | fp::FieldFlags::Settable, nullptr, nullptr, "micro-seconds"},
    {&servoDrv2.maxPulseUsec[0], "max_pulses",  kNumServos,  fp::FieldDataType::Uint,  2,  fp::FieldFlags::Gettable | fp::FieldFlags::Settable, nullptr, nullptr, "micro-seconds"},
};
fp::FieldTable servosTable2 = {
    .fields = servosFields2,
    .numFields = sizeof(servosFields2)/sizeof(fp::FieldEntry)
};

// ==================================== //
// Temperature sensors

typedef struct sTemperatureSensorDriver {
    float temperature;
} TemperatureSensorDriver;

// Sensor 1
static TemperatureSensorDriver tempDrv1 = {};
fp::FieldEntry temperatureSensorFields1[] = {
   { &tempDrv1.temperature, "temperature",       1,  fp::FieldDataType::Float,  4,  fp::FieldFlags::Gettable,  nullptr, nullptr, ""},
};
fp::FieldTable temperatureSensorTable1= {
    .fields = temperatureSensorFields1,
    .numFields = sizeof(temperatureSensorFields1)/sizeof(fp::FieldEntry)
};

// Sensor 2
static TemperatureSensorDriver tempDrv2 = {};
fp::FieldEntry temperatureSensorFields2[] = {
   { &tempDrv2.temperature, "temperature",       1,  fp::FieldDataType::Float,  4,  fp::FieldFlags::Gettable,  nullptr, nullptr, ""},
};
fp::FieldTable temperatureSensorTable2= {
    .fields = temperatureSensorFields2,
    .numFields = sizeof(temperatureSensorFields2)/sizeof(fp::FieldEntry)
};

// ==================================== //

static fp::BoardInfo customNodeBoardInfo[kMasterNodeSystemNumNodes] = {
    {"usb",                "usb",                 0xaaa1,     sizeof(usbFields)/sizeof(fp::FieldEntry)},
    {"battery",            "battery",             0xaaa2,     sizeof(batteryFields)/sizeof(fp::FieldEntry)},
    {"button",             "button",              0xaaa3,     sizeof(buttonFields)/sizeof(fp::FieldEntry)},
    {"dcmotors",           "dcmotors",            0xaaa4,     sizeof(dcmotorsFields)/sizeof(fp::FieldEntry)},
    {"distance_sensor",    "distance_sensor",     0xaaa5,     sizeof(distanceSensorFields)/sizeof(fp::FieldEntry)},
    {"imu",                "imu",                 0xaaa6,     sizeof(imuFields)/sizeof(fp::FieldEntry)},
    {"joystick",           "joystick",            0xaaa7,     sizeof(joystickFields)/sizeof(fp::FieldEntry)},
    {"led_strip",          "led_strip",           0xaaa8,     sizeof(ledStripFields)/sizeof(fp::FieldEntry)},
    {"rotary_encoder",     "rotary_encoder",      0xaaa9,     sizeof(rotaryEncoderFields)/sizeof(fp::FieldEntry)},
    {"screen",             "screen",              0xaaaa,     sizeof(screenFields)/sizeof(fp::FieldEntry)},
    {"servos",             "servos",              0xaaab,     sizeof(servosFields1)/sizeof(fp::FieldEntry)},
    {"servos",             "servos",              0xaaac,     sizeof(servosFields2)/sizeof(fp::FieldEntry)},
    {"temperature_sensor", "temperature_sensor",  0xaaad,     sizeof(temperatureSensorFields1)/sizeof(fp::FieldEntry)},
    {"temperature_sensor", "temperature_sensor",  0xaaae,     sizeof(temperatureSensorFields2)/sizeof(fp::FieldEntry)},
};

fp::FieldTable * customNodeTables[kMasterNodeSystemNumNodes] = {
    &usbTable,
    &batteryTable,
    &buttonTable,
    &dcmotorsTable,
    &distanceSensorTable,
    &imuTable,
    &joystickTable,
    &ledStripTable,
    &rotaryEncoderTable,
    &screenTable,
    &servosTable1,
    &servosTable2,
    &temperatureSensorTable1,
    &temperatureSensorTable2
};


// ==================================== //
// Randomly generated boards
//
// Generated into the System's own arena and board arrays, so each System owns
// its boards and frees them (via the arena's destructor) when it goes out of
// scope - no shared global state and no manual free between iterations.

static void createRandomBoards(System& sys, uint32_t numNodes) {
    for (uint32_t n = 0; n < 1 + numNodes; n++) {
        fp::BoardInfo* boardInfo = &sys.randomBoardInfo[n];
        createRandomBoardInfo(boardInfo, fpTestRand() % 40);  // Max 40 fields per board
        // FieldEntry owns std::string members, so the entries are allocated with
        // operator new[] (which runs constructors); the arena owns and frees them.
        fp::FieldTable* table = sys.arena.allocTable();
        table->numFields = boardInfo->numFields;
        table->fields = sys.arena.allocEntries(boardInfo->numFields);
        for (uint32_t i = 0; i < boardInfo->numFields; i++) {
            createRandomFieldEntry(sys.arena, &table->fields[i], table->numFields);
        }
        sys.randomTables[n] = table;
    }
}

// ==================================== //

void buildSystem(System& sys, NetworkSim& sim, bool useRandomBoards, uint32_t numNodes) {
    sys.sim = &sim;
    sys.numNodes = numNodes;
    sys.cycles = 0;
    std::memset(sys.ignoreNode, 0, sizeof(sys.ignoreNode));

    // Mark which nodes are present (index 0 is the master) and bring the fabric up.
    bool nodeActive[fp::kMaxNumNodes] = {false};
    for (uint8_t n = 0; n < numNodes + 1; n++) {
        nodeActive[n] = true;
    }
    sim.initNetwork(nodeActive);

    if (useRandomBoards) {
        assert(numNodes <= fp::kMaxFpDaemonBoards);
        createRandomBoards(sys, numNodes);
        sys.boardInfo = sys.randomBoardInfo;
        sys.tables = sys.randomTables;
    } else {
        sys.boardInfo = customNodeBoardInfo;
        sys.tables = customNodeTables;
    }

    sys.master.init(sim.masterDaemonItf(), sim.masterNodeBusItf(), sys.boardInfo[0], &sys.tables[0], 1,
                    sim.connectedNodesFn(), sim.numTxInUsbBufferFn(), sim.maxRxCredits(),
                    sim.maxTxCredits());

    // FieldEntry/BoardInfo hold std::string, so each node is reset by assigning a
    // fresh optional rather than memset; init() re-seats the active ones.
    for (uint32_t n = 1; n < 1 + numNodes; n++) {
        sys.nodes[n].emplace(fp::Node());
        sys.nodes[n]->init(sim.nodeBusItf(n), sys.boardInfo[n], &sys.tables[n], 1);
    }
    for (uint32_t n = 1 + numNodes; n < fp::kMaxNumNodes; n++) {
        sys.nodes[n].reset();
    }
}
