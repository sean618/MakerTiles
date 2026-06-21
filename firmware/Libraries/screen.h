/* Copyright (c) 2024 Sean Bremner */
#include "project.h"
#ifdef ENABLE_SCREEN_MODULE

#include "FieldProtocol/fpCommon.hpp"

extern const fp::FieldTable screenFieldTable;

void initialiseScreen(GPIO_TypeDef* powerGpioSection,
                    uint16_t powerGpioPin, 
                    GPIO_TypeDef* csGpioSection,
                    uint16_t csGpioPin,
                    GPIO_TypeDef* dcGpioSection,
                    uint16_t dcGpioPin,
                    GPIO_TypeDef* resetGpioSection,
                    uint16_t resetGpioPin,
                    GPIO_TypeDef* backlightGpioSection,
                    uint16_t backlightGpioPin, 
                    SPI_HandleTypeDef * spi);

void loopScreen(uint32_t ticks);

#endif //ENABLE_SERVO_MODULE
