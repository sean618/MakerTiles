
#include "string.h"
#include "stdbool.h"
#include "stdint.h"
#include "stdio.h"
#include "stdlib.h"

#include "project.h"
#include "stm32g0xx_hal.h"
#include "stm32/stm32Node.h"
#include "Libraries/button.h"

extern SPI_HandleTypeDef hspi2;
extern TIM_HandleTypeDef htim1;
extern TIM_HandleTypeDef htim3;

const fp::FieldTable * fieldTables[] = {
    &buttonFieldTable,
    &assertFieldTable
};
fp::BoardInfo boardInfo = {
    .boardName = "button_exp",
    .customName = "button_exp",
    .uniqueId = 0,
    .numFields = 0
};

extern "C" void boardInit() {
    GPIO_TypeDef * buttonGpioSections[] = {
        GPIOA,
        GPIOA,
        GPIOB,
        GPIOB,
        GPIOA,

        GPIOA,
        GPIOA,
        GPIOB,
        GPIOB,
        GPIOB,

        GPIOB,

        GPIOA,
        GPIOA,
        GPIOB,
        GPIOA,
        GPIOA,

        GPIOA,
        GPIOC,
        GPIOC,
        GPIOB,
        GPIOB,

        GPIOB,

    };
    uint16_t buttonGpioPins[] = {
        GPIO_PIN_15,
        GPIO_PIN_11,
        GPIO_PIN_2,
        GPIO_PIN_0,
        GPIO_PIN_6,

        GPIO_PIN_4,
        GPIO_PIN_2,
        GPIO_PIN_7,
        GPIO_PIN_6,
        GPIO_PIN_5,

        GPIO_PIN_3,

        GPIO_PIN_12,
        GPIO_PIN_8,
        GPIO_PIN_1,
        GPIO_PIN_7,
        GPIO_PIN_5,

        GPIO_PIN_3,
        GPIO_PIN_15,
        GPIO_PIN_14,
        GPIO_PIN_9,
        GPIO_PIN_8,

        GPIO_PIN_4,

    };
    initButton(&htim1, buttonGpioSections, buttonGpioPins, true);
	protocolInit(fieldTables, sizeof(fieldTables)/sizeof(fp::FieldTable *), &boardInfo, &hspi2, 2, GPIOA, GPIO_PIN_9, &htim3, GPIO_PIN_1);
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
   loopButton(HAL_GetTick());
}
