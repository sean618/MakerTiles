
#include "string.h"
#include "stdbool.h"
#include "stdint.h"
#include "stdio.h"
#include "stdlib.h"

#include "project.h"
#include "stm32g0xx_hal.h"
#include "stm32/stm32Node.h"
#include "Libraries/screen.h"

extern SPI_HandleTypeDef hspi1;
extern SPI_HandleTypeDef hspi2;
// extern TIM_HandleTypeDef htim1;
extern TIM_HandleTypeDef htim3;



const fp::FieldTable * fieldTables[] = {
    &screenFieldTable,
    &assertFieldTable
};
fp::BoardInfo boardInfo = {
    .boardName = "screen",
    .customName = "screen",
    .uniqueId = 0,
    .numFields = 0
};

extern "C" void boardInit() {
    // Set IM pins
    // IM0:0, IM1:1, IM2:1
    // 0:PC14, 1:PC15, 2:PA2
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_14, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_15, GPIO_PIN_SET);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_2,  GPIO_PIN_SET);

    initialiseScreen(
        NULL, NULL, // Power
        GPIOA, GPIO_PIN_11, // CS
        GPIOA, GPIO_PIN_12,// DC
        GPIOB, GPIO_PIN_0, // Reset
        GPIOB, GPIO_PIN_3, // Backlight
        &hspi2
    );
	protocolInit(fieldTables, sizeof(fieldTables)/sizeof(fp::FieldTable *), &boardInfo, &hspi1, 1, GPIOA, GPIO_PIN_6, &htim3, GPIO_PIN_1);
}

extern "C" void boardLoop() {    
   loopScreen(HAL_GetTick());
}
