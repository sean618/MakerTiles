/* Copyright (c) 2024 Sean Bremner */
#include "string.h"
#include "stdbool.h"
#include "stdint.h"
#include "stdio.h"
#include "stdlib.h"

#include "project.h"
#include "stm32g0xx_hal.h"
#include "stm32/stm32Node.h"
#include "Libraries/joystick.h"
#include "Libraries/button.h"

extern SPI_HandleTypeDef hspi1;
// extern SPI_HandleTypeDef hspi2;
extern TIM_HandleTypeDef htim1;
extern TIM_HandleTypeDef htim3;
extern ADC_HandleTypeDef hadc1;



const fp::FieldTable * fieldTables[] = {
    &joystickFieldTable,
    &buttonFieldTable,
    &assertFieldTable
};
fp::BoardInfo boardInfo = {
    .boardName = "joystick",
    .customName = "joystick",
    .uniqueId = 0,
    .numFields = 0
};

extern "C" void boardInit() {
    initialiseJoystick(&hadc1);
    GPIO_TypeDef* buttonGpioSections[] = {GPIOA};
    uint16_t buttonGpioPins[] = {GPIO_PIN_2};
    initButton(&htim1, buttonGpioSections, buttonGpioPins, false);
	protocolInit(fieldTables, sizeof(fieldTables)/sizeof(fp::FieldTable *), &boardInfo, &hspi1, 1, GPIOA, GPIO_PIN_6, &htim3, GPIO_PIN_1);
}

extern "C" void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim) {
   if (htim == &htim3) {
        // Called every 0.2ms
       protocol_TIM_PeriodElapsedCallback();
   } else if (htim == &htim1) {
        // Called every 1ms
       button_TIM_PeriodElapsedCallback();
   }
}

extern "C" void boardLoop() {    
   loopJoystick(HAL_GetTick());
   loopButton(HAL_GetTick());
}
