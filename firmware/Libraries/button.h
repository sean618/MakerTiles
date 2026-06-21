#include "project.h"

#ifdef ENABLE_BUTTONS_MODULE

#include "FieldProtocol/fpCommon.hpp"

extern const fp::FieldTable buttonFieldTable;

void initButton(TIM_HandleTypeDef * tim, GPIO_TypeDef* buttonGpioSections[], uint16_t buttonGpioPins[], bool pulledUp);
void loopButton(uint32_t ticks);
void button_TIM_PeriodElapsedCallback();

#endif
