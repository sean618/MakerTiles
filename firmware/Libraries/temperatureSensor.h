/* Copyright (c) 2024 Sean Bremner */
#include "project.h"
#ifdef ENABLE_TEMPERATURE_MODULE

#include "FieldProtocol/fpCommon.hpp"

extern const fp::FieldTable temperatureSensorFieldTable;

void initTemperatureSensor(I2C_HandleTypeDef *hi2c);
void loopTemperatureSensor(uint32_t ticks);

#endif //ENABLE_IMU_MODULE
