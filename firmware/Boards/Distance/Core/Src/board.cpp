
#include "string.h"
#include "stdbool.h"
#include "stdint.h"
#include "stdio.h"
#include "stdlib.h"

#include "stm32f1xx_hal.h"
#include "stm32/stm32Node.h"
#include "Libraries/distanceSensor.h"

extern SPI_HandleTypeDef hspi1;
extern TIM_HandleTypeDef htim3;
extern I2C_HandleTypeDef hi2c1;

const fp::FieldTable * fieldTables[] = {
    &distanceFieldTable,
    &assertFieldTable
};
fp::BoardInfo boardInfo = {
    .boardName = "distance_sensor",
    .customName = "distance_sensor",
    .uniqueId = 0,
    .numFields = 0
};

extern "C" void boardInit() {
    initDistanceSensor(GPIOA, GPIO_PIN_2, &hi2c1);
	protocolInit(fieldTables, sizeof(fieldTables)/sizeof(fp::FieldTable *), &boardInfo, &hspi1, 1, GPIOB, GPIO_PIN_4, &htim3, GPIO_PIN_8);
}

extern "C" void boardLoop() {
   loopDistanceSensor(HAL_GetTick());
}


extern "C" void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
//	if (GPIO_Pin == spiNode.psGpioPin) {
		protocol_GPIO_EXTI_Rising_Callback();
//	}
}
