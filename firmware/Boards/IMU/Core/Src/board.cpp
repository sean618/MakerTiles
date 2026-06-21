
#include "string.h"
#include "stdbool.h"
#include "stdint.h"
#include "stdio.h"
#include "stdlib.h"

#include "project.h"
#include "stm32g0xx_hal.h"
#include "stm32/stm32Node.h"
#include "Libraries/imu.h"

extern SPI_HandleTypeDef hspi1;
// extern TIM_HandleTypeDef htim1;
extern TIM_HandleTypeDef htim3;
//extern UART_HandleTypeDef huart2;
extern I2C_HandleTypeDef hi2c1;

const fp::FieldTable * fieldTables[] = {
    &imuFieldTable,
	&assertFieldTable
};
fp::BoardInfo boardInfo = {
    .boardName = "imu",
    .customName = "imu",
    .uniqueId = 0,
    .numFields = 0
};

extern "C" void boardInit() {
    initImu(
        GPIOA, GPIO_PIN_0, // Reset
        GPIOA, GPIO_PIN_4, // Bootloader
        GPIOA, GPIO_PIN_11, // PS1, 
        GPIOA, GPIO_PIN_12, // Bootload Indicator, 
        // &huart2 //  uart
        &hi2c1
    );
	protocolInit(fieldTables, sizeof(fieldTables)/sizeof(fp::FieldTable *), &boardInfo, &hspi1, 1, GPIOA, GPIO_PIN_6, &htim3, GPIO_PIN_1);
}

extern "C" void boardLoop() {    
   loopImu(HAL_GetTick());
}
