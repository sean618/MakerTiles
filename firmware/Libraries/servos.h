/* Copyright (c) 2024 Sean Bremner */
#include "project.h"
#ifdef ENABLE_SERVO_MODULE

#include "FieldProtocol/fpCommon.hpp"

extern const fp::FieldTable servosFieldTable;

void initServos(TIM_HandleTypeDef * tims[], uint32_t timChannels[]);
void loopServos(uint32_t ticks);

#endif //ENABLE_SERVO_MODULE
