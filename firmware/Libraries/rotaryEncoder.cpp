#include "project.h"
#ifdef ENABLE_ROTARY_ENCODER_MODULE

#include <stdio.h>
#include <math.h>
#include <string.h>
#include "rotaryEncoder.h"

// HW setup:
// For each encoder set up 2 interrupt pins to trigger on rising or falling edge

typedef struct sRotaryEncoderDriver {
    GPIO_TypeDef * portA;
    GPIO_TypeDef * portB;
    uint16_t pinA;
    uint16_t pinB;
    int32_t realPosition;
    int32_t userPosition;
    int32_t min;
    int32_t max;
    // uint8_t lastState;
    bool lastStateA;
    bool lastStateB;
    uint32_t missedCount;
} RotaryEncoderDriver;

RotaryEncoderDriver rotaryDrv = {0};

const fp::FieldEntry rotaryEncoderFields[] = {
    {&rotaryDrv.userPosition,   "position",  1,  fp::FieldDataType::Int,      4,  fp::FieldFlags::Gettable | fp::FieldFlags::Settable, NULL, NULL, NULL},
    {&rotaryDrv.min,        "min",       1,  fp::FieldDataType::Int,      4,  fp::FieldFlags::Gettable | fp::FieldFlags::Settable, NULL, NULL, NULL},
    {&rotaryDrv.max,        "max",       1,  fp::FieldDataType::Int,      4,  fp::FieldFlags::Gettable | fp::FieldFlags::Settable, NULL, NULL, NULL},
};

const fp::FieldTable rotaryEncoderFieldTable = {
    .fields = (fp::FieldEntry*) rotaryEncoderFields,
    .numFields = sizeof(rotaryEncoderFields)/sizeof(fp::FieldEntry)
};


int8_t encoderDirection(bool lastStateA, bool lastStateB, bool stateA, bool stateB) {
    uint8_t state = ((!!stateA) << 1) | (!!stateB);
    uint8_t last = ((!!lastStateA) << 1) | (!!lastStateB);

    // 00 -> 01 -> 11 -> 10 -> 00
    // A: ___/~~~\___
    // B: _/~~~\_____
    if ((last == 0b00 && state == 0b01) ||
        (last == 0b01 && state == 0b11) ||
        (last == 0b11 && state == 0b10) ||
        (last == 0b10 && state == 0b00)) {
        return 1;

    // A: _/~~~\_____
    // B: ___/~~~\___
    } else if ((last == 0b00 && state == 0b10) ||
               (last == 0b10 && state == 0b11) ||
               (last == 0b11 && state == 0b01) ||
               (last == 0b01 && state == 0b00)) {
        return -1;
    }

    if (last != state) {
        // It's either moved 2 forwards or 2 backwards - can't tell (or even a multiple of 2)
        rotaryDrv.missedCount++;
    }
    return 0; // Hasn't changed
}

void rotary_TIM_PeriodElapsedCallback() {
    // Allow the user to set the userPosition - in this we need to adjust the original realPosition
    if (rotaryDrv.userPosition != (rotaryDrv.realPosition / 2)) {
        rotaryDrv.realPosition = rotaryDrv.userPosition * 2;
    }
    bool stateA = (HAL_GPIO_ReadPin(rotaryDrv.portA, rotaryDrv.pinA) == GPIO_PIN_SET);
    bool stateB = (HAL_GPIO_ReadPin(rotaryDrv.portB, rotaryDrv.pinB) == GPIO_PIN_SET);
    int8_t change = encoderDirection(rotaryDrv.lastStateA, rotaryDrv.lastStateB, stateA, stateB);
    rotaryDrv.realPosition += change;
    rotaryDrv.userPosition = rotaryDrv.realPosition / 2;
    rotaryDrv.lastStateA = stateA;
    rotaryDrv.lastStateB = stateB;

    if (rotaryDrv.userPosition < rotaryDrv.min) {
        rotaryDrv.userPosition = rotaryDrv.min;
    }
    if (rotaryDrv.userPosition > rotaryDrv.max) {
        rotaryDrv.userPosition = rotaryDrv.max;
    }
}

// Using interrupts doesn't seem to work!
// void encoderExtiCallback(uint16_t GPIO_Pin, bool rising) {
//     bool stateA = rotaryDrv.intlastStateA;
//     bool stateB = rotaryDrv.intlastStateB;
//     if (GPIO_Pin == rotaryDrv.pinA) {
//         stateA = !stateA;
//     } else if(GPIO_Pin == rotaryDrv.pinB) {
//         stateB = !stateB;
//     } else {
//         return;
//     }
//     int8_t change = encoderDirection(rotaryDrv.intlastStateA, rotaryDrv.intlastStateB, stateA, stateB);
//     rotaryDrv.intrealPosition += change;
//     rotaryDrv.intlastStateA = stateA;
//     rotaryDrv.intlastStateB = stateB;
// }


void initRotaryEncoder(GPIO_TypeDef * portA, uint16_t pinA, GPIO_TypeDef * portB, uint16_t pinB) {
    rotaryDrv.portA = portA;
    rotaryDrv.portB = portB;
    rotaryDrv.pinA = pinA;
    rotaryDrv.pinB = pinB;
    rotaryDrv.min = -1000000;
    rotaryDrv.max = 10000000;

    // rotaryDrv.lastStateA = (HAL_GPIO_ReadPin(rotaryDrv.portA, rotaryDrv.pinA) == GPIO_PIN_SET);
    // rotaryDrv.lastStateB = (HAL_GPIO_ReadPin(rotaryDrv.portB, rotaryDrv.pinB) == GPIO_PIN_SET);
}

void loopRotaryEncoder(uint32_t ticks) {
}


#endif

