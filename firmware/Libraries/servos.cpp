#include "project.h"
#ifdef ENABLE_SERVO_MODULE

#include <stdio.h>
#include <math.h>
#include <string.h>


// TIMERS (50us period):
//    Prescalar: 47
//    Counter period: 20000

// PWM channel:
//    Mode:  PWM mode 1
//    Output compare preload:  enable

typedef struct sservoBoardDriver {
    bool initialised;
	TIM_HandleTypeDef * tim[NUM_SERVOS];
	uint32_t channel[NUM_SERVOS];
	uint16_t minPulseUsec[NUM_SERVOS];
	uint16_t maxPulseUsec[NUM_SERVOS];
    uint32_t angle[NUM_SERVOS];
} servoBoardDriver;

servoBoardDriver servoDrv = {};


void setServoAngle(uint16_t servoIndex, uint32_t angle) {
    uint16_t i = servoIndex;
    uint32_t angleDegreeHundreth = (angle * 100);
	softAssert(angleDegreeHundreth <= (180 * 100), "Angle can't be more than 180 degrees");
    servoDrv.angle[i] = angle;
    uint16_t min = servoDrv.minPulseUsec[i];
    uint16_t max = servoDrv.maxPulseUsec[i];
    uint16_t pulseUs = (min + ((angleDegreeHundreth * (max - min)) / (180 * 100)));
	__HAL_TIM_SET_COMPARE(servoDrv.tim[i], servoDrv.channel[i], pulseUs);
	// printf("Setting angle to %d degrees\r\n", angleDegrees);
}

static bool setServoAngleField(fp::FieldEntry * field, fp::FieldIndex fieldOffset, fp::FieldIndex numFields, uint8_t * data) {
    for (uint32_t i=fieldOffset; i<fieldOffset+numFields; i++) {
        uint32_t index = i;
        if (index >= NUM_SERVOS) {
            softAssert(0, "Invalid servo index");
            return false;
        }
        uint32_t angle;
        memcpy(&angle, data, 4);
        if (angle > 180) {
            softAssert(0, "Invalid servo angle");
            return false;
        }
        setServoAngle(index, angle);
        servoDrv.angle[index] = angle;
    }
    return true;
}

const fp::FieldEntry servosFields[] = {
    {&servoDrv.angle[0],        "angles",      NUM_SERVOS,  fp::FieldDataType::Uint,  4,  fp::FieldFlags::Gettable | fp::FieldFlags::Settable, &setServoAngleField, NULL, "degrees"},
    {&servoDrv.minPulseUsec[0], "min_pulses",  NUM_SERVOS,  fp::FieldDataType::Uint,  2,  fp::FieldFlags::Gettable | fp::FieldFlags::Settable, NULL,                NULL, "micro-seconds"},
    {&servoDrv.maxPulseUsec[0], "max_pulses",  NUM_SERVOS,  fp::FieldDataType::Uint,  2,  fp::FieldFlags::Gettable | fp::FieldFlags::Settable, NULL,                NULL, "micro-seconds"},
};
const fp::FieldTable servosFieldTable = {
    .fields = (fp::FieldEntry*) servosFields,
    .numFields = sizeof(servosFields)/sizeof(fp::FieldEntry)
};


// ======================== //

void initServos(TIM_HandleTypeDef * tims[], uint32_t timChannels[]) {
    for (uint8_t i=0; i<NUM_SERVOS; i++) {
        servoDrv.minPulseUsec[i] = 544;
        servoDrv.maxPulseUsec[i] = 2500;
        servoDrv.tim[i] = tims[i];
        servoDrv.channel[i] = timChannels[i];
        HAL_StatusTypeDef status = HAL_TIM_PWM_Start(tims[i], timChannels[i]);
		softAssert(status == HAL_OK, "Failed to start PWM");
    }
}

void loopServos(uint32_t ticks) {
}

#endif //ENABLE_SERVO_MODULE
