
#include "string.h"
#include "stdbool.h"
#include "stdint.h"
#include "stdio.h"
#include "stdlib.h"

#include "stm32g0xx_hal.h"
#include "Libraries/ledStrip.h"
#include "stm32/stm32Node.h"

extern SPI_HandleTypeDef hspi1;
extern TIM_HandleTypeDef htim1;
extern TIM_HandleTypeDef htim3;

const fp::FieldTable * fieldTables[] = {
    &ledStripFieldTable,
    &assertFieldTable
};
fp::BoardInfo boardInfo = {
    .boardName = "led_matrix",
    .customName = "led_matrix",
    .uniqueId = 0,
    .numFields = 0
};


extern "C" void HAL_TIM_PWM_PulseFinishedCallback(TIM_HandleTypeDef *htim) {
    ledStrip_TIM_PWM_PulseFinishedCallback();
}
extern "C" void HAL_TIM_PWM_PulseFinishedHalfCpltCallback(TIM_HandleTypeDef *htim) {
    ledStrip_TIM_PWM_PulseFinishedHalfCpltCallback();
}

extern "C" void boardInit() {
    initialiseLedStrip(&htim1, TIM_CHANNEL_1, NULL, 0);
	protocolInit(fieldTables, sizeof(fieldTables)/sizeof(fp::FieldTable *), &boardInfo, &hspi1, 1, GPIOA, GPIO_PIN_6, &htim3, GPIO_PIN_1);
}

extern "C" void boardLoop() {    
   loopLedStrip(HAL_GetTick());
}

