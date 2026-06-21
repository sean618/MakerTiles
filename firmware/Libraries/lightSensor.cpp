#include "project.h"

#ifdef ENABLE_LIGHT_SENSOR_MODULE

#include <stdio.h>
#include <math.h>
#include <string.h>
#include "project.h"
// #include "../FieldProtocol/fpNode.h"


#define LTR381_I2C_ADDR       0x53

// Register addresses
#define LTR381_REG_MAIN_CTRL     0x00
#define LTR381_REG_ALS_MEAS_RATE 0x04
#define LTR381_REG_ALS_GAIN      0x05
#define LTR381_REG_PART_ID       0x06
#define LTR381_REG_MAIN_STATUS   0x07

#define LTR381_REG_DATA_IR     0x0A 
#define LTR381_REG_DATA_GREEN  0x0D
#define LTR381_REG_DATA_RED    0x10
#define LTR381_REG_DATA_BLUE   0x13
#define LTR381_REG_DATA_AMBIENT_LOW    0x21
#define LTR381_REG_DATA_AMBIENT_HIGH   0x24

typedef struct {
    I2C_HandleTypeDef * i2c;
    uint8_t gain;
    uint32_t ambientLow;
    uint32_t ambientHigh;
    uint32_t red;
    uint32_t green;
    uint32_t blue;
    uint32_t ir;
    uint32_t readCounts;
} sLightSensorDriver;

sLightSensorDriver lightDrv = {0};

void ltrI2cWrite(uint8_t reg, uint8_t val) {
    volatile HAL_StatusTypeDef result = HAL_I2C_Mem_Write(lightDrv.i2c, LTR381_I2C_ADDR << 1, reg, I2C_MEMADD_SIZE_8BIT, &val, 1, 200);
    myAssert(result == HAL_OK, "");
}

void ltrI2cRead(uint8_t reg, uint8_t *buf, uint8_t len) {
    volatile HAL_StatusTypeDef result = HAL_I2C_Mem_Read(lightDrv.i2c, LTR381_I2C_ADDR << 1, reg, I2C_MEMADD_SIZE_8BIT, buf, len, 200);
    myAssert(result == HAL_OK, "");
}

static bool setGain(fp::FieldEntry * field, fp::FieldIndex fieldIndex, fp::FieldIndex numFields, uint8_t * data) {
    // 1,3,6,9,18
    uint8_t allowedValues[5] = {1, 3, 6, 9, 18};
    for (uint32_t i=0; i<5; i++) {
        if (*data == allowedValues[i]) {
            ltrI2cWrite(LTR381_REG_ALS_GAIN, *data);
        }
    }
    return true;
}

fp::FieldEntry lightFields[] = {
    {&lightDrv.ambientLow,  "gain",          1, fp::FieldDataType::Uint,   2,  fp::FieldFlags::Gettable | fp::FieldFlags::Settable, &setGain, NULL, NULL},
    {&lightDrv.ambientLow,  "ambient_low",   1, fp::FieldDataType::Uint,   3,  fp::FieldFlags::Gettable, NULL, NULL, NULL},
    {&lightDrv.ambientHigh, "ambient_high",  1, fp::FieldDataType::Uint,   3,  fp::FieldFlags::Gettable, NULL, NULL, NULL},
    {&lightDrv.red,         "red",           1, fp::FieldDataType::Uint,   3,  fp::FieldFlags::Gettable, NULL, NULL, NULL},
    {&lightDrv.green,       "green",         1, fp::FieldDataType::Uint,   3,  fp::FieldFlags::Gettable, NULL, NULL, NULL},
    {&lightDrv.blue,        "blue",          1, fp::FieldDataType::Uint,   3,  fp::FieldFlags::Gettable, NULL, NULL, NULL},
    {&lightDrv.ir,          "ir",            1, fp::FieldDataType::Uint,   3,  fp::FieldFlags::Gettable, NULL, NULL, NULL},
    
};
const fp::FieldTable lightSensorFieldTable = {
    .fields = (fp::FieldEntry*) lightFields,
    .numFields = sizeof(lightFields)/sizeof(fp::FieldEntry)
};


void ltr381Init(void) {
    // ID check
    uint8_t id;
    ltrI2cRead(LTR381_REG_PART_ID, &id, 1);
    myAssert(id == 0xC2, "");

    // Power on, ALS enable
    ltrI2cWrite(LTR381_REG_MAIN_CTRL, 0x06);
    // Set ALS rate (100ms), ALS mode
    ltrI2cWrite(LTR381_REG_ALS_MEAS_RATE, 0x22);
    // Set gain (1x)
    ltrI2cWrite(LTR381_REG_ALS_GAIN, 0x01);
}

static uint32_t convert(uint8_t data[3]) {
    return (((uint32_t)data[2] & 0x0F) << 16) 
        | ((uint32_t)data[1] << 8)
        | (uint32_t)data[0];
}

void ltr381Read() {
	uint8_t data[3*4];
    ltrI2cRead(LTR381_REG_DATA_IR,  &data[0], 3*4);
    lightDrv.ir      = convert(&data[0]);
    lightDrv.green   = convert(&data[3]);
    lightDrv.red     = convert(&data[6]);
    lightDrv.blue    = convert(&data[9]);

    ltrI2cRead(LTR381_REG_DATA_IR,  &data[0], 3*2);
    lightDrv.ambientLow   = convert(&data[0]);
    lightDrv.ambientHigh  = convert(&data[3]);
    lightDrv.readCounts++;
}


// ======================== //

void initLightSensor(I2C_HandleTypeDef * i2c) {
    lightDrv.i2c = i2c;
    ltr381Init();
    ltr381Read();
}


void loopLightSensor(uint32_t ticks) {
    static uint32_t nextTicks = 0;
    // Check every 10ms
    if (ticks > nextTicks) {
        nextTicks = ticks + 100;

        uint8_t status;
        ltrI2cRead(LTR381_REG_MAIN_STATUS, &status, 1);
        if (status & 0x08) {
            ltr381Read();
        }
    }
}

 #endif
