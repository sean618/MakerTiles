
#include "string.h"
#include "stdbool.h"
#include "stdint.h"
#include "stdio.h"
#include "stdlib.h"

#include "project.h"
#include "stm32g0xx_hal.h"
#include "stm32/stm32Node.h"
#include "Libraries/battery.h"

extern SPI_HandleTypeDef hspi1;
// extern TIM_HandleTypeDef htim1;
extern TIM_HandleTypeDef htim3;
extern ADC_HandleTypeDef hadc1;

// Read stdby (PB0), charge (PB3)
// Read battery voltage - PA0
// Read battery current - PA2
// Switch on/off boost enable (PB7)
// Switch on/off USB 5V (PA3)

const fp::FieldTable * fieldTables[] = {
    &batteryFieldTable,
    &assertFieldTable
};
fp::BoardInfo boardInfo = {
    .boardName = "battery",
    .customName = "battery",
    .uniqueId = 0,
    .numFields = 0
};

extern "C" void boardInit() {
    initialiseBattery(
        GPIOB,
        GPIO_PIN_0,
        GPIOB,
        GPIO_PIN_3,
        GPIOA,
        GPIO_PIN_3,
        &hadc1
    );
	protocolInit(fieldTables, sizeof(fieldTables)/sizeof(fp::FieldTable *), &boardInfo, &hspi1, 1, GPIOA, GPIO_PIN_6, &htim3, GPIO_PIN_1);
}

extern "C" void boardLoop() {    
   loopBattery(HAL_GetTick());
}
