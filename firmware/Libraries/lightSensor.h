#include "project.h"

#ifdef ENABLE_LIGHT_SENSOR_MODULE

#include "FieldProtocol/fpCommon.hpp"

extern const fp::FieldTable lightSensorFieldTable;

void initLightSensor(I2C_HandleTypeDef * i2c);
void loopLightSensor(uint32_t ticks);

#endif
