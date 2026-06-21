
#include "project.h"
#ifdef ENABLE_TEMPERATURE_MODULE

#include <stdio.h>
#include <math.h>
#include <string.h>
#include <stdint.h>
#include "External/tmp102.h"

typedef struct sTemperatureSensorDriver {
    tmp102_device dev;
    float temperature;
} TemperatureSensorDriver;

static TemperatureSensorDriver tempDrv = {};

// ================================================ //

const fp::FieldEntry temperatureSensorFields[] = {
   { &tempDrv.temperature, "temperature",       1,  fp::FieldDataType::Float,  4,  fp::FieldFlags::Gettable,  NULL, NULL, NULL},
};
const fp::FieldTable temperatureSensorFieldTable = {
    .fields = (fp::FieldEntry*) temperatureSensorFields,
    .numFields = sizeof(temperatureSensorFields)/sizeof(fp::FieldEntry)
};

// ======================== //



void initTemperatureSensor(I2C_HandleTypeDef *hi2c) {
    myAssert(HAL_OK == tmp102Initialize(&tempDrv.dev, hi2c, TMP102_ADD0_GROUND), "");
}

void loopTemperatureSensor(uint32_t ticks) {
    static uint32_t nextTicks = 0;
    if (ticks > nextTicks) {
        nextTicks = ticks + 10;
        myAssert(HAL_OK == tmp102UpdateCachedTemperatureRegister(&tempDrv.dev), "");
        tempDrv.temperature = tmp102GetTemperature(&tempDrv.dev, TMP102_TEMPERATURE_REG);
    }
}

#endif
