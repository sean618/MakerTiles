/* Copyright (c) 2024 Sean Bremner */
#include "project.h"
#ifdef ENABLE_DCMOTOR_MODULE

#include "FieldProtocol/fpCommon.hpp"

extern const fp::FieldTable dcmotorsFieldTable;

void initDCMotors(TIM_HandleTypeDef * tim1s[], uint32_t tim1Channels[], TIM_HandleTypeDef * tim2s[], uint32_t tim2Channels[], uint16_t encoderPin1[], uint16_t encoderPin2[]);
void loopDCMotors(uint32_t ticks);
void motor_GPIO_EXTI_Rising_Callback(uint16_t GPIO_Pin);

#endif //ENABLE_DCMOTOR_MODULE
