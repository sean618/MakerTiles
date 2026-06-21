
#ifndef LEDSTRIP_H
#define LEDSTRIP_H

#include <stdint.h>
#include "project.h"
#include "FieldProtocol/fpCommon.hpp"

extern const fp::FieldTable ledStripFieldTable;

void initialiseLedStrip(TIM_HandleTypeDef * tim, uint32_t timChannel, GPIO_TypeDef* testGpioX, uint16_t testGpioPin);
void loopLedStrip(uint32_t ticks);
void ledStrip_TIM_PWM_PulseFinishedCallback();
void ledStrip_TIM_PWM_PulseFinishedHalfCpltCallback();

#endif // ENABLE_LED_STRIP
