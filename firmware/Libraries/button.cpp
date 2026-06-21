#include "project.h"

#ifdef ENABLE_BUTTONS_MODULE

#include <stdio.h>
#include <math.h>
#include <string.h>
#include "button.h"

// HW setup:
//   - GPIO input for each button
//   - Timer set with a period of 1ms

typedef struct {
    GPIO_TypeDef * gpioSection[NUM_BUTTONS];
    uint16_t gpioPin[NUM_BUTTONS];
    uint8_t pressed[NUM_BUTTONS];
    uint32_t pressedCount[NUM_BUTTONS];
    bool pulledUp;
    TIM_HandleTypeDef * tim;
} sButtonBoardDriver;


static sButtonBoardDriver buttonsDrv = {0};


const fp::FieldEntry buttonFields[] = {
    {&buttonsDrv.pressed[0],      "pressed",         NUM_BUTTONS, fp::FieldDataType::Boolean,   1,  fp::FieldFlags::Gettable | fp::FieldFlags::Settable, NULL, NULL, NULL},
    {&buttonsDrv.pressedCount[0], 
#if NUM_BUTTONS == 1
        "pressed_count",
#else
        "pressed_counts",
#endif
                                                     NUM_BUTTONS, fp::FieldDataType::Uint,      4,  fp::FieldFlags::Gettable | fp::FieldFlags::Settable, NULL, NULL, NULL},
};
const fp::FieldTable buttonFieldTable = {
    .fields = (fp::FieldEntry*) buttonFields,
    .numFields = sizeof(buttonFields)/sizeof(fp::FieldEntry)
};


// ======================== //

#define DEBOUNCE_MSEC 10

static uint8_t getDebouncedButtonPressed(uint16_t * count, uint8_t rawPressed) {
    // Increment time "count"
    (*count)++;
    // If the button is no longer set - reset the count
    if (!rawPressed) {
        *count = 0;
    }
    // If the count has remained stable for enough time register it as set
    if (*count >= DEBOUNCE_MSEC) {
        return 1;
    }
    return 0;
}

static void checkButtons() {
    static uint16_t count[NUM_BUTTONS] = {};
    for (uint8_t i=0; i<NUM_BUTTONS; i++) {
        GPIO_PinState readValue = HAL_GPIO_ReadPin(buttonsDrv.gpioSection[i], buttonsDrv.gpioPin[i]);
        uint8_t pressed = getDebouncedButtonPressed(&count[i], (readValue == (buttonsDrv.pulledUp ? GPIO_PIN_RESET : GPIO_PIN_SET)));
        if (pressed && !buttonsDrv.pressed[i]) {
            buttonsDrv.pressedCount[i]++;
        }
        buttonsDrv.pressed[i] = pressed;
    }
}

void button_TIM_PeriodElapsedCallback() {
    checkButtons();
}

// uint8_t selfTestButtons() {
//     // Just check that the buttons haven't been pressed
//     // This should be called close to power on so no one should have pressed anything yet
//     uint8_t failed = 0;
//     for (uint8_t i=0; i<NUM_BUTTONS; i++) {
//         failed |= (buttonsDrv.pressedCount[i] > 0) || buttonsDrv.pressed[i];
//     }
//     softAssert(!failed, "Button self test failed");
// 	return failed;
// }

void initButton(TIM_HandleTypeDef * tim, GPIO_TypeDef* buttonGpioSections[], uint16_t buttonGpioPins[], bool pulledUp) {
    buttonsDrv.tim = tim;
    for (uint8_t i=0; i<NUM_BUTTONS; i++) {
        buttonsDrv.gpioPin[i] = buttonGpioPins[i];
        buttonsDrv.gpioSection[i] = buttonGpioSections[i];
    }
    buttonsDrv.pulledUp = pulledUp;
    volatile HAL_StatusTypeDef status = HAL_TIM_Base_Start_IT(tim);
    softAssert(status == HAL_OK, "Failed");

    // HAL_Delay(100);
    // selfTestButtons();
}


void loopButton(uint32_t ticks) {
    // if (!buttonsDrv.initialised) {
    //     return;
    // }
    // static uint32_t ticksSinceLastPublish = 0;
    // // Publish any changes as soon as they happen
    // static uint32_t lastPressedCount[NUM_BUTTONS] = {};
    // for (uint8_t i=0; i<NUM_BUTTONS; i++) {
    //     if (buttonsDrv.pressedCount[i] != lastPressedCount[i]) {
    //         lastPressedCount[i] = buttonsDrv.pressedCount[i];
    //         publishButtonFields(i);
    //     }
    // }
    // // Publish every second
    // if (ticks - ticksSinceLastPublish > 1000) {
    //     ticksSinceLastPublish = ticks;
    //     publishFieldsIfBelowBandwidth(BUTTONS_FIELDS_OFFSET, BUTTONS_FIELDS_OFFSET + (2*NUM_BUTTONS) -1);
    // }
}

 #endif
