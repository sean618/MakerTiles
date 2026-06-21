/* Copyright (c) 2024 Sean Bremner */
#include "project.h"
#ifdef ENABLE_DISTANCE_MODULE

#include "FieldProtocol/fpCommon.hpp"

extern const fp::FieldTable distanceFieldTable;

void initDistanceSensor(GPIO_TypeDef* enableGpio, uint16_t enableGpioPin, I2C_HandleTypeDef * i2c);
void loopDistanceSensor(uint32_t ticks);

#endif //ENABLE_IMU_MODULE
