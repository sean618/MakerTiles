/* Copyright (c) 2024 Sean Bremner */
#include "project.h"
#ifdef ENABLE_JOYSTICK_MODULE

#include <stdio.h>
#include <math.h>
#include <string.h>
#include <stdint.h>

typedef struct sJoystickDriver {
    uint16_t horizontal;
    uint16_t lastHorizontal;
    uint16_t vertical;
    uint16_t lastVertical;
    ADC_HandleTypeDef * adc;
} JoystickDriver;

static JoystickDriver joystick = {};

const fp::FieldEntry joystickFields[] = {
    { &joystick.horizontal,   "horizontal",   1,  fp::FieldDataType::Uint,   2,  fp::FieldFlags::Gettable,  NULL, NULL, "degrees"},
    { &joystick.vertical,     "vertical",     1,  fp::FieldDataType::Uint,   2,  fp::FieldFlags::Gettable,  NULL, NULL, "degrees"},
};

const fp::FieldTable joystickFieldTable = {
    .fields = (fp::FieldEntry*) joystickFields,
    .numFields = sizeof(joystickFields)/sizeof(fp::FieldEntry)
};

// ======================== //

void initialiseJoystick(ADC_HandleTypeDef * adc) {
    joystick.adc = adc;    
}

void loopJoystick(uint32_t ticks) {
    static uint32_t nextTicks = 0;
    static uint32_t nextPublishTicks = 0;
    
    if (ticks > nextTicks) {
        nextTicks = ticks + 5;
        uint32_t adcVal[2];
        myAssert(HAL_OK == HAL_ADC_Start(joystick.adc), "");
        for (uint32_t i=0; i<2; i++) {
        	volatile HAL_StatusTypeDef res= HAL_ADC_PollForConversion(joystick.adc, 50);
            myAssert(HAL_OK == res, "");
            adcVal[i] = HAL_ADC_GetValue(joystick.adc);
        }
        HAL_ADC_Stop(joystick.adc);
        joystick.vertical = adcVal[0];
        joystick.horizontal = adcVal[1];
    }
}

#endif //ENABLE_JOYSTICK_MODULE
