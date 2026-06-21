#include "project.h"

#ifdef ENABLE_BATTERY_MODULE

#include "FieldProtocol/fpCommon.hpp"

extern const fp::FieldTable batteryFieldTable;

void initialiseBattery(
        GPIO_TypeDef * standbyGpio,
        uint16_t standbyPin,
        GPIO_TypeDef * chargeGpio,
        uint16_t chargePin,
        GPIO_TypeDef * enable5vGpio,
        uint16_t enable5vPin,
        ADC_HandleTypeDef *adc);
void loopBattery(uint32_t ticks);

#endif
