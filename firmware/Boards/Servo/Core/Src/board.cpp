
#include "string.h"
#include "stdbool.h"
#include "stdint.h"
#include "stdio.h"
#include "stdlib.h"

#include "project.h"
#include "stm32g0xx_hal.h"
#include "stm32/stm32Node.h"
#include "Libraries/servos.h"

extern SPI_HandleTypeDef hspi2;
extern TIM_HandleTypeDef htim3;
extern TIM_HandleTypeDef htim1;
extern TIM_HandleTypeDef htim14;
extern TIM_HandleTypeDef htim16;
extern TIM_HandleTypeDef htim17;

const fp::FieldTable * fieldTables[] = {
    &servosFieldTable,
    &assertFieldTable
};
fp::BoardInfo boardInfo = {
    .boardName = "servos",
    .customName = "servos",
    .uniqueId = 0,
    .numFields = 0
};

extern "C" void boardInit() {
    TIM_HandleTypeDef * tims[] = {&htim1,        &htim3,         &htim1,         &htim3,         &htim3,         &htim3,        &htim16,         &htim14,         };
    uint32_t timChannels[] =     {TIM_CHANNEL_4, TIM_CHANNEL_1,  TIM_CHANNEL_1,  TIM_CHANNEL_4,  TIM_CHANNEL_3,  TIM_CHANNEL_2, TIM_CHANNEL_1,   TIM_CHANNEL_1,  };
    initServos(tims, timChannels);
	protocolInit(fieldTables, sizeof(fieldTables)/sizeof(fp::FieldTable *), &boardInfo, &hspi2, 2, GPIOA, GPIO_PIN_9, &htim17, GPIO_PIN_1);
}

extern "C" void boardLoop() {    
   loopServos(HAL_GetTick());
}
