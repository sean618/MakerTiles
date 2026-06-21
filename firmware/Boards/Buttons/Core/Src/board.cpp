
#include "string.h"
#include "stdbool.h"
#include "stdint.h"
#include "stdio.h"
#include "stdlib.h"

#include "project.h"
#include "stm32g0xx_hal.h"
#include "stm32/stm32Node.h"
#include "Libraries/button.h"

extern SPI_HandleTypeDef hspi1;
extern TIM_HandleTypeDef htim1;
extern TIM_HandleTypeDef htim3;

const fp::FieldTable * fieldTables[] = {
    &buttonFieldTable,
    &assertFieldTable
};
fp::BoardInfo boardInfo = {
    .boardName = "buttons",
    .customName = "buttons",
    .uniqueId = 0,
    .numFields = 0
};

/*
Left        - 0 - SW2 - PA12
Top         - 1 - SW1 - PA11
Right       - 2 - SW4 - PB0
Down        - 3 - SW5 - PA3
Far right   - 4 - SW3 - PA4
*/

extern "C" void boardInit() {
    GPIO_TypeDef * buttonGpioSections[] = {GPIOA,       GPIOA,       GPIOB,       GPIOA,       GPIOA};
    uint16_t buttonGpioPins[]           = {GPIO_PIN_12, GPIO_PIN_11, GPIO_PIN_0, GPIO_PIN_3, GPIO_PIN_4};
    initButton(&htim1, buttonGpioSections, buttonGpioPins, false);
	protocolInit(fieldTables, sizeof(fieldTables)/sizeof(fp::FieldTable *), &boardInfo, &hspi1, 1, GPIOA, GPIO_PIN_6, &htim3, GPIO_PIN_1);
}

// Called every 0.2ms
extern "C" void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim) {
   if (htim == &htim3) {
       protocol_TIM_PeriodElapsedCallback();
   } else if (htim == &htim1) {
       button_TIM_PeriodElapsedCallback();
   }
}

extern "C" void boardLoop() {    
   loopButton(HAL_GetTick());
}
