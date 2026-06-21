#include "project.h"
#ifdef ENABLE_IMU_MODULE

#include <stdio.h>
#include <math.h>
#include <string.h>
#include <stdint.h>
#include "bno055.h"
#include "imu.h"

typedef struct {
    int16_t x;
    int16_t y;
    int16_t z;
} tGeneric3int16;

typedef struct sIMUBoardDriver {
    #ifdef IMU_I2C
        I2C_HandleTypeDef *  i2c;
    #else
        UART_HandleTypeDef * uart;
    #endif
    struct bno055_euler_float_t         orientation;
    struct bno055_linear_accel_float_t  acceleration;
    struct bno055_gravity_float_t       gravity;
    struct bno055_gyro_float_t          angularVelocity;
    struct bno055_t bno055;
    uint32_t sumGravityX100;
} IMUBoardDriver;

IMUBoardDriver imu = {0};

//    ptr,                           name,            numFields,  data_type,       data_size,  gettable,  settable,  setfn,  getfn,  units
const fp::FieldEntry imuFields[] = {
    { &imu.orientation.p,            "pitch",                 1,  fp::FieldDataType::Float,  4,  fp::FieldFlags::Gettable, NULL, NULL, "degrees" }, //  p:  -180 -> 180 
    { &imu.orientation.r,            "roll",                  1,  fp::FieldDataType::Float,  4,  fp::FieldFlags::Gettable, NULL, NULL, "degrees" }, //  r:  -90 -> 90 
    { &imu.orientation.h,            "yaw",                   1,  fp::FieldDataType::Float,  4,  fp::FieldFlags::Gettable, NULL, NULL, "degrees" }, //  h:  0 -> 360 
    { &imu.acceleration.x,           "acceleration_x",        1,  fp::FieldDataType::Float,  4,  fp::FieldFlags::Gettable, NULL, NULL, "m/s^2"   }, 
    { &imu.acceleration.y,           "acceleration_y",        1,  fp::FieldDataType::Float,  4,  fp::FieldFlags::Gettable, NULL, NULL, "m/s^2"   }, 
    { &imu.acceleration.z,           "acceleration_z",        1,  fp::FieldDataType::Float,  4,  fp::FieldFlags::Gettable, NULL, NULL, "m/s^2"   }, 
    { &imu.angularVelocity.x,        "angular_velocity_x",    1,  fp::FieldDataType::Float,  4,  fp::FieldFlags::Gettable, NULL, NULL, "m/s"     }, 
    { &imu.angularVelocity.y,        "angular_velocity_y",    1,  fp::FieldDataType::Float,  4,  fp::FieldFlags::Gettable, NULL, NULL, "m/s"     }, 
    { &imu.angularVelocity.z,        "angular_velocity_z",    1,  fp::FieldDataType::Float,  4,  fp::FieldFlags::Gettable, NULL, NULL, "m/s"     }, 
    { &imu.gravity.x,                "gravity_x",             1,  fp::FieldDataType::Float,  4,  fp::FieldFlags::Gettable, NULL, NULL, NULL      }, 
    { &imu.gravity.y,                "gravity_y",             1,  fp::FieldDataType::Float,  4,  fp::FieldFlags::Gettable, NULL, NULL, NULL      }, 
    { &imu.gravity.z,                "gravity_z",             1,  fp::FieldDataType::Float,  4,  fp::FieldFlags::Gettable, NULL, NULL, NULL      },
    // { &imu.temp,                          "chip_temperature",     IMU_FIELDS_OFFSET + 12,  1,  AF_FIELD_TYPE_FLOAT,  4,  0,  &temperatureMetaData,     NULL, NULL},
};
const fp::FieldTable imuFieldTable = {
    .fields = (fp::FieldEntry*) imuFields,
    .numFields = sizeof(imuFields)/sizeof(fp::FieldEntry)
};

static void BNO055_delay_msec(unsigned msec) {
    HAL_Delay(msec);
}

#ifndef IMU_I2C

int8_t BNO055_uart_bus_write(uint8_t dev_addr, uint8_t reg_addr, uint8_t *reg_data, uint8_t cnt) {
    uint8_t request[50] = {};
    softAssert(cnt < 40, "More than 40 bytes needed");
    request[0] = 0xAA;
    request[1] = 0x00;
    request[2] = reg_addr;
    request[3] = cnt;
    memcpy(&request[4], reg_data, cnt);
    HAL_StatusTypeDef statusW = HAL_UART_Transmit(imu.uart, request, cnt+4, 100);
    softAssert(statusW == HAL_OK, "uart write failed");

    uint8_t response[2] = {};
    HAL_StatusTypeDef statusR = HAL_UART_Receive(imu.uart, response, 2, 1000);
    softAssert(statusR == HAL_OK, "uart read failed");
    softAssert(response[0] == 0xEE, "Response byte incorrect");

//    softAssert(response[1] == 0x01, "Write failed");
    if (response[1] == 0x01) {
        HAL_StatusTypeDef statusW = HAL_UART_Transmit(imu.uart, request, cnt+4, 100);
        softAssert(statusW == HAL_OK, "uart write failed");

        HAL_StatusTypeDef statusR = HAL_UART_Receive(imu.uart, response, 2, 1000);
        softAssert(statusR == HAL_OK, "uart read failed");
        softAssert(response[0] == 0xEE, "Response byte incorrect");
        softAssert(response[1] == 0x01, "Write failed");
    }
    return ((statusW == HAL_OK && statusR == HAL_OK) ? 0 : -1);
}
int8_t BNO055_uart_bus_read_recursively(uint8_t dev_addr, uint8_t reg_addr, uint8_t *reg_data, uint8_t cnt, uint8_t recursiveDepth) {
    uint8_t request[4] = {};
    request[0] = 0xAA;
    request[1] = 0x01;
    request[2] = reg_addr;
    request[3] = cnt;
    HAL_StatusTypeDef statusW = HAL_UART_Transmit(imu.uart, request, 4, 100);
    softAssert(statusW == HAL_OK, "uart write failed");

    uint8_t response[50] = {};
    softAssert(cnt < 40, "More than 40 bytes needed");
    HAL_StatusTypeDef statusR = HAL_UART_Receive(imu.uart, response, cnt+2, 1000);

    if (statusR != HAL_OK && response[0] == 0xEE && response[1] == 7) {
        // Retry up to 3 times - the BNO055 mcu is probably just busy
    	HAL_Delay(2);
        recursiveDepth++;
        if (recursiveDepth == 3) {
            return -1;
        }
        return BNO055_uart_bus_read_recursively(dev_addr, reg_addr, reg_data, cnt, recursiveDepth);
    } else {
        softAssert(statusR == HAL_OK, "uart read failed");
        softAssert(response[0] == 0xBB, "Response byte incorrect");
        softAssert(response[1] == cnt, "length incorrect");
        memcpy(reg_data, &response[2], cnt);
        return ((statusW == HAL_OK && statusR == HAL_OK)  ? 0 : -1);
    }
}
int8_t BNO055_uart_bus_read(uint8_t dev_addr, uint8_t reg_addr, uint8_t *reg_data, uint8_t cnt) {
    return BNO055_uart_bus_read_recursively(dev_addr, reg_addr, reg_data, cnt, 0);
}

#endif

#ifdef IMU_I2C

static int8_t BNO055_I2C_bus_write(uint8_t dev_addr, uint8_t reg_addr, uint8_t *reg_data, uint8_t cnt) {
    HAL_StatusTypeDef status = HAL_I2C_Mem_Write(imu.i2c, dev_addr << 1, reg_addr, I2C_MEMADD_SIZE_8BIT, reg_data, cnt, 100);
     softAssert(status == HAL_OK, "I2C write failed");
    return ((status == HAL_OK) ? 0 : -1);
}

static int8_t BNO055_I2C_bus_read(uint8_t dev_addr, uint8_t reg_addr, uint8_t *reg_data, uint8_t cnt) {
    HAL_StatusTypeDef status = HAL_I2C_Mem_Read(imu.i2c, dev_addr << 1, reg_addr, I2C_MEMADD_SIZE_8BIT, reg_data, cnt, 100);
     softAssert(status == HAL_OK, "I2C read failed");
    return ((status == HAL_OK) ? 0 : -1);
}

#endif

void readAndProcess() {
    // readMultipleTimesAndGetClosestValue2(&imu.orientation, readOrientation, 3); - debug
    // 3.7ms for 3 reads
    
    struct bno055_gyro_t angularVelocityInt;
    struct bno055_linear_accel_t accelerationInt;
    struct bno055_gravity_t gravityInt;
    struct bno055_euler_t orientationInt;

    softAssert(BNO055_SUCCESS == bno055_read_gyro_xyz(&angularVelocityInt), "Read gyro failed");
    softAssert(BNO055_SUCCESS == bno055_read_linear_accel_xyz(&accelerationInt), "Read acceleration failed");
    softAssert(BNO055_SUCCESS == bno055_read_gravity_xyz(&gravityInt), "Read gravity failed");
    softAssert(BNO055_SUCCESS == bno055_read_euler_hrp(&orientationInt), "Read orientation failed");
    
    imu.sumGravityX100 = gravityInt.x + gravityInt.y + gravityInt.z;

    softAssert(orientationInt.p > (-180 * 16) && orientationInt.p < (180 * 16), "Invalid pitch");
    softAssert(orientationInt.r > (-180 * 16) && orientationInt.r < (180 * 16), "Invalid roll");
    softAssert(orientationInt.h >= 0 && orientationInt.h < (16 * 360), "Invalid yaw");
    softAssert(gravityInt.x < (10*100) && gravityInt.x > (-10 * 100), "Invalid gravity");
    softAssert(gravityInt.y < (10*100) && gravityInt.y > (-10 * 100), "Invalid gravity");
    softAssert(gravityInt.z < (10*100) && gravityInt.z > (-10 * 100), "Invalid gravity");

    imu.angularVelocity.x = angularVelocityInt.x / BNO055_GYRO_DIV_DPS;
    imu.angularVelocity.y = angularVelocityInt.y / BNO055_GYRO_DIV_DPS;
    imu.angularVelocity.z = angularVelocityInt.z / BNO055_GYRO_DIV_DPS;

    imu.acceleration.x = accelerationInt.x / BNO055_LINEAR_ACCEL_DIV_MSQ;
    imu.acceleration.y = accelerationInt.y / BNO055_LINEAR_ACCEL_DIV_MSQ;
    imu.acceleration.z = accelerationInt.z / BNO055_LINEAR_ACCEL_DIV_MSQ;

    imu.gravity.x = gravityInt.x / BNO055_GRAVITY_DIV_MSQ;
    imu.gravity.y = gravityInt.y / BNO055_GRAVITY_DIV_MSQ;
    imu.gravity.z = gravityInt.z / BNO055_GRAVITY_DIV_MSQ;

    imu.orientation.r = orientationInt.r / BNO055_EULER_DIV_DEG;
    imu.orientation.h = orientationInt.h / BNO055_EULER_DIV_DEG;
    imu.orientation.p = orientationInt.p / BNO055_EULER_DIV_DEG;
}

// ======================== //

uint8_t testImu() {
    readAndProcess();
    // Just check Gravity values are sensible
    uint8_t failed = (imu.sumGravityX100 < 900 || imu.sumGravityX100 > 2000);
    softAssert(!failed, "IMU self test failed");
    return failed;
}

void initImu(GPIO_TypeDef* resetGpio, uint16_t resetGpioPin,
                    GPIO_TypeDef* bootloaderGpio, uint16_t bootloaderGpioPin, 
                    GPIO_TypeDef* ps1Gpio, uint16_t ps1GpioPin, 
                    GPIO_TypeDef* bootloadIndicatorGpio, uint16_t bootloadIndicatorGpioPin, 
                    #ifdef IMU_I2C
                        I2C_HandleTypeDef * i2c
                    #else
                        UART_HandleTypeDef * uart
                    #endif
                    ) {
        
    #ifdef IMU_I2C
        imu.i2c = i2c;
        imu.bno055.bus_write = BNO055_I2C_bus_write;
        imu.bno055.bus_read = BNO055_I2C_bus_read;
        HAL_GPIO_WritePin(ps1Gpio, ps1GpioPin, GPIO_PIN_RESET);
    #else
        imu.uart = uart;
        imu.bno055.bus_write = BNO055_uart_bus_write;
        imu.bno055.bus_read = BNO055_uart_bus_read;
        HAL_GPIO_WritePin(ps1Gpio, ps1GpioPin, GPIO_PIN_SET);
    #endif
            
    // Ensure bootloader is not selected
    HAL_GPIO_WritePin(bootloaderGpio, bootloaderGpioPin, GPIO_PIN_SET);
    HAL_Delay(10);
    // Apply reset
    HAL_GPIO_WritePin(resetGpio, resetGpioPin, GPIO_PIN_RESET);
    HAL_Delay(1000);
    // Release reset
    HAL_GPIO_WritePin(resetGpio, resetGpioPin, GPIO_PIN_SET);
    HAL_Delay(1000);
    softAssert(HAL_GPIO_ReadPin(bootloadIndicatorGpio, bootloadIndicatorGpioPin) == GPIO_PIN_RESET, "Bootloader indicator not set");

    imu.bno055.delay_msec = BNO055_delay_msec;
    imu.bno055.dev_addr = BNO055_I2C_ADDR1;
    
    softAssert(bno055_init(&imu.bno055) == BNO055_SUCCESS, "bno055_init failed");
    softAssert(bno055_set_power_mode(BNO055_POWER_MODE_NORMAL) == BNO055_SUCCESS, "set_power_mode failed");
    softAssert(bno055_set_operation_mode(BNO055_OPERATION_MODE_NDOF) == BNO055_SUCCESS, "set_operation_mode failed");
    softAssert(bno055_set_euler_unit(BNO055_EULER_UNIT_DEG) == BNO055_SUCCESS, "set_euler_unit failed");
    softAssert(bno055_set_gyro_unit(BNO055_GYRO_UNIT_DPS) == BNO055_SUCCESS, "set_gyro_unit failed");
}

void loopImu(uint32_t ticks) {
    static uint32_t nextTicks = 0;
    if (ticks > nextTicks) {
        nextTicks = ticks + 100;
        readAndProcess();
        // publishFieldsIfBelowBandwidth(0, 11);
    }
}

#endif //ENABLE_IMU_MODULE
