/*
 * Copyright © 2024 - 2024 Sean Bremner. All rights reserved.
 *
 * LedStrip node board, ported to the C++ FieldProtocol + MicroBus libraries.
 * The shared node driver (protocol.cpp) does the heavy lifting; this just
 * declares the board's field tables and starts the LED driver + protocol.
 */

#include <cstdint>

#include "stm32g0xx_hal.h"

#include "FieldProtocol/fpCommon.hpp"
#include "Libraries/ledStrip.h"
#include "stm32/stm32Node.h"
#include "Libraries/myAssert.h"

extern SPI_HandleTypeDef hspi1;
extern TIM_HandleTypeDef htim1;
extern TIM_HandleTypeDef htim3;

const fp::FieldTable* fieldTables[] = {
    &ledStripFieldTable,
    &assertFieldTable,
};

fp::BoardInfo boardInfo = {
    "led_strip",  // boardName
    "led_strip",  // customName
    0,            // uniqueId
    0,            // numFields (filled in by init)
};

extern "C" void HAL_TIM_PWM_PulseFinishedCallback(TIM_HandleTypeDef* htim) {
    ledStrip_TIM_PWM_PulseFinishedCallback();
}
extern "C" void HAL_TIM_PWM_PulseFinishedHalfCpltCallback(TIM_HandleTypeDef* htim) {
    ledStrip_TIM_PWM_PulseFinishedHalfCpltCallback();
}

extern "C" void boardInit() {
    initialiseLedStrip(&htim1, TIM_CHANNEL_1, nullptr, 0);
    protocolInit(fieldTables, sizeof(fieldTables) / sizeof(fp::FieldTable*), &boardInfo,
                 &hspi1, 1, GPIOA, GPIO_PIN_6, &htim3, GPIO_PIN_1);
}

extern "C" void boardLoop() {
    loopLedStrip(HAL_GetTick());
}
