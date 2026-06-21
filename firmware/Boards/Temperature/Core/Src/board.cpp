
#include "string.h"
#include "stdbool.h"
#include "stdint.h"
#include "stdio.h"
#include "stdlib.h"

#include "stm32g0xx_hal.h"

#include "project.h"
#include "Libraries/temperatureSensor.h"
#include "stm32/stm32Node.h"

extern SPI_HandleTypeDef hspi1;
extern TIM_HandleTypeDef htim3;

extern I2C_HandleTypeDef hi2c1;

const fp::FieldTable * fieldTables[] = {
    &temperatureSensorFieldTable,
    &assertFieldTable
};
fp::BoardInfo boardInfo = {
    .boardName = "temperatureSensor",
    .customName = "temperatureSensor",
    .uniqueId = 0,
    .numFields = 0
};


extern "C" void boardInit() {
    initTemperatureSensor(&hi2c1);
    protocolInit(fieldTables, sizeof(fieldTables)/sizeof(fp::FieldTable *), &boardInfo, &hspi1, 1, GPIOA, GPIO_PIN_6, &htim3, GPIO_PIN_1);
}

extern "C" void boardLoop() {    
   loopTemperatureSensor(HAL_GetTick());
}

