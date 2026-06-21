
#ifndef PROTOCOL_H
#define PROTOCOL_H

#include "project.h"
#include "FieldProtocol/fpCommon.hpp"

// Node-side protocol entry points. Consumed by the (C++) board layer.
void protocolInit(
        const fp::FieldTable* fieldTables[],
        uint8_t numTables,
        fp::BoardInfo* boardInfo,
        SPI_HandleTypeDef* hspi,
        uint32_t spiIndex,
        GPIO_TypeDef* misoGpioX,
        uint16_t misoGpioPin,
        TIM_HandleTypeDef* htim,
        uint16_t psGpioPin
    );
void protocol_GPIO_EXTI_Rising_Callback();
void protocol_TIM_PeriodElapsedCallback(); // Every 200us

// Proactively publish fields (streaming) from a driver. Replaces fpNodeSendFields.
void protocolSendFields(fp::FieldIndex startField, fp::FieldIndex numFields);

#endif
