/* Copyright (c) 2024 Sean Bremner */
#include "project.h"
#include <stdint.h>

#ifdef ENABLE_IMU_MODULE

#include "bno055.h"

#define IMU_I2C

#include "FieldProtocol/fpCommon.hpp"

extern const fp::FieldTable imuFieldTable;

void initImu(
    GPIO_TypeDef* resetGpio, uint16_t resetGpioPin,
    GPIO_TypeDef* bootloaderGpio, uint16_t bootloaderGpioPin, 
    GPIO_TypeDef* ps1Gpio, uint16_t ps1GpioPin, 
    GPIO_TypeDef* bootloadIndicatorGpio, uint16_t bootloadIndicatorGpioPin, 
    #ifdef IMU_I2C
        I2C_HandleTypeDef * i2c
    #else
        UART_HandleTypeDef * uart
    #endif
);

void loopImu();

#endif //ENABLE_IMU_MODULE
