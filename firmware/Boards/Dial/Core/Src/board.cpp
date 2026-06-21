
#include "string.h"
#include "stdbool.h"
#include "stdint.h"
#include "stdio.h"
#include "stdlib.h"

#include "project.h"
#include "stm32g0xx_hal.h"
#include "stm32/stm32Node.h"
#include "Libraries/rotaryEncoder.h"
#include "Libraries/button.h"

extern SPI_HandleTypeDef hspi1;
extern TIM_HandleTypeDef htim1;
extern TIM_HandleTypeDef htim3;

const fp::FieldTable * fieldTables[] = {
    &rotaryEncoderFieldTable,
    &buttonFieldTable,
    &assertFieldTable
};
fp::BoardInfo boardInfo = {
    .boardName = "dial",
    .customName = "dial",
    .uniqueId = 0,
    .numFields = 0
};

static inline void GPIO_EXTI_Callback(uint16_t GPIO_Pin, bool rising) {
    if (rising && (GPIO_Pin == GPIO_PIN_1)) {
        protocol_GPIO_EXTI_Rising_Callback();
    }
    //  else if (GPIO_Pin == GPIO_PIN_11 || GPIO_Pin == GPIO_PIN_12) {
    //     encoderExtiCallback(GPIO_Pin, rising);
    // }
}

extern "C" void HAL_GPIO_EXTI_Rising_Callback(uint16_t GPIO_Pin) {
    GPIO_EXTI_Callback(GPIO_Pin, true);
}
extern "C" void HAL_GPIO_EXTI_Falling_Callback(uint16_t GPIO_Pin) {
    GPIO_EXTI_Callback(GPIO_Pin, false);
}

extern "C" void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim) {
   if (htim == &htim3) {
        // Called every 0.2ms
        rotary_TIM_PeriodElapsedCallback();
        protocol_TIM_PeriodElapsedCallback();
   } else if (htim == &htim1) {
        // Called every 1ms
        button_TIM_PeriodElapsedCallback();
   }
}

extern "C" void boardInit() {
    GPIO_TypeDef * buttonGpioSections[] = {GPIOA};
    uint16_t buttonGpioPins[] = {GPIO_PIN_3};
    initButton(&htim1, buttonGpioSections, buttonGpioPins, false);
    initRotaryEncoder(GPIOA, GPIO_PIN_11, GPIOA, GPIO_PIN_12);
    protocolInit(fieldTables, sizeof(fieldTables)/sizeof(fp::FieldTable *), &boardInfo, &hspi1, 1, GPIOA, GPIO_PIN_6, &htim3, GPIO_PIN_1);
}

extern "C" void boardLoop() {    
    loopRotaryEncoder(HAL_GetTick());
    loopButton(HAL_GetTick());
}
