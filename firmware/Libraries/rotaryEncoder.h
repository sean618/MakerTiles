/* Copyright (c) 2024 Sean Bremner */
#include "project.h"
#include <stdint.h>

#ifdef ENABLE_ROTARY_ENCODER_MODULE

#include "FieldProtocol/fpCommon.hpp"

extern const fp::FieldTable rotaryEncoderFieldTable;

void initRotaryEncoder(GPIO_TypeDef * portA, uint16_t pinA, GPIO_TypeDef * portB, uint16_t pinB);
void loopRotaryEncoder(uint32_t ticks);
void rotary_TIM_PeriodElapsedCallback();
// void encoderExtiCallback(uint16_t GPIO_Pin, bool rising);

#endif
