#include "project.h"

#ifdef ENABLE_SPEAKER_MODULE

#include "FieldProtocol/fpCommon.hpp"

extern const fp::FieldTable speakerFieldTable;

void initSpeaker(
        // TIM_HandleTypeDef * pwmTim,
        // uint32_t pwmTimChannel,
        I2S_HandleTypeDef * i2s,
        GPIO_TypeDef* i2sShutDownGpio,
        uint16_t i2sShutDownGpioPin
        // GPIO_TypeDef* pwmShutDownGpio,
        // uint16_t pwmShutDownGpioPin,
        // DAC_HandleTypeDef *dac,
        //  uint32_t dacChannel,
        // TIM_HandleTypeDef * dacTim
);
void loopSpeaker(uint32_t ticks);

#endif
