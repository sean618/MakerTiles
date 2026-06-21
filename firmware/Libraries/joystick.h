/* Copyright (c) 2024 Sean Bremner */
#include "project.h"
#ifdef ENABLE_JOYSTICK_MODULE

#include "FieldProtocol/fpCommon.hpp"

extern const fp::FieldTable joystickFieldTable;

void initialiseJoystick(ADC_HandleTypeDef * adc);
void loopJoystick(uint32_t ticks);

#endif //ENABLE_JOYSTICK_MODULE