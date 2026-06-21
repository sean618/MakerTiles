
#include "string.h"
#include "stdbool.h"
#include "stdint.h"
#include "stdio.h"
#include "stdlib.h"

#include "project.h"
#include "stm32g0xx_hal.h"
#include "stm32/stm32Node.h"
#include "Libraries/dcmotors.h"

extern SPI_HandleTypeDef hspi1;
extern TIM_HandleTypeDef htim1;
extern TIM_HandleTypeDef htim16;
extern TIM_HandleTypeDef htim17;
extern TIM_HandleTypeDef htim3;

const fp::FieldTable * fieldTables[] = {
    &dcmotorsFieldTable,
    &assertFieldTable
};
fp::BoardInfo boardInfo = {
    .boardName = "motors",
    .customName = "motors",
    .uniqueId = 0,
    .numFields = 0
};


extern "C" void boardInit() {

    // TIM_HandleTypeDef * tim1s[] = {&htim1, &htim1};
    // uint32_t tim1Channels[] = {TIM_CHANNEL_2, TIM_CHANNEL_1};
    // TIM_HandleTypeDef * tim2s[] = {&htim17, &htim16};
    // uint32_t tim2Channels[] = {TIM_CHANNEL_1, TIM_CHANNEL_1};
    
    //                              Motor1, motor2, ...
    TIM_HandleTypeDef * tim1s[] = {&htim1, &htim17};
    uint32_t tim1Channels[]     = {TIM_CHANNEL_2, TIM_CHANNEL_1};
    TIM_HandleTypeDef * tim2s[] = {&htim1, &htim16};
    uint32_t tim2Channels[]     = {TIM_CHANNEL_1, TIM_CHANNEL_1};


    uint16_t encoderPin1[] = {GPIO_PIN_11, GPIO_PIN_2};
    uint16_t encoderPin2[] = {GPIO_PIN_12, GPIO_PIN_3};
    initDCMotors(tim1s, tim1Channels, tim2s, tim2Channels, encoderPin1, encoderPin2);
	protocolInit(fieldTables, sizeof(fieldTables)/sizeof(fp::FieldTable *), &boardInfo, &hspi1, 1, GPIOA, GPIO_PIN_6, &htim3, GPIO_PIN_1);
}

extern "C" void boardLoop() {    
   loopDCMotors(HAL_GetTick());
}

extern "C" void HAL_GPIO_EXTI_Rising_Callback(uint16_t GPIO_Pin) {
    if (GPIO_Pin == GPIO_PIN_1) {
        protocol_GPIO_EXTI_Rising_Callback();
    } else {
        motor_GPIO_EXTI_Rising_Callback(GPIO_Pin);
    }
}
