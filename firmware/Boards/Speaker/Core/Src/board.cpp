
#include "string.h"
#include "stdbool.h"
#include "stdint.h"
#include "stdio.h"
#include "stdlib.h"

#include "project.h"
#include "stm32g0xx_hal.h"
#include "Libraries/speaker.h"
#include "stm32/stm32Node.h"

extern SPI_HandleTypeDef hspi2;
extern TIM_HandleTypeDef htim1;
extern TIM_HandleTypeDef htim3;
extern I2S_HandleTypeDef hi2s1;

const fp::FieldTable * fieldTables[] = {
    &speakerFieldTable,
    &assertFieldTable
};
fp::BoardInfo boardInfo = {
    .boardName = "speaker",
    .customName = "speaker",
    .uniqueId = 0,
    .numFields = 0
};


extern "C" void boardInit() {

    initSpeaker(&hi2s1, GPIOA, GPIO_PIN_11);
	protocolInit(fieldTables, sizeof(fieldTables)/sizeof(fp::FieldTable *), &boardInfo, &hspi2, 2, GPIOA, GPIO_PIN_3, &htim3, GPIO_PIN_1);
}

extern "C" void boardLoop() {
   loopSpeaker(HAL_GetTick());
}
