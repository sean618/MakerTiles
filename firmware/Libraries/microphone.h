#include "project.h"

#ifdef ENABLE_MICROPHONE_MODULE

#include "FieldProtocol/fpCommon.hpp"

extern const fp::FieldTable microphoneFieldTable;

void initMicrophone(TIM_HandleTypeDef * tim, ADC_HandleTypeDef * adc);
void loopMicrophone(uint32_t ticks);

#endif
