#include "project.h"
#ifdef ENABLE_DCMOTOR_MODULE

#include <stdio.h>
#include <math.h>
#include <string.h>


#ifndef DCMOTOR_FIELDS_OFFSET
    #define DCMOTOR_FIELDS_OFFSET 0
#endif

// TODO: try 5KHz instead - better low duty cycle performance - although worse ripple - maybe switch frequency from 5K to 20K depending on <20%, <40%, <60, <100

// Use PWM frequency of 20KHz - 50us - counter period - 48*50

// Note: at 15% voltage will rotate however once stopped won't start again

#define COUNTER_PERIOD (2400)


// TIMERS:
//    Prescalar: 0
//    Counter period: 2400 

// PWM channels:
//    Mode:  PWM mode 1
//    Output compare preload:  enable

// External interrupts on rising edge for each encoder pin

#define RPM_SMOOTHING_BUFFER 10 // Smooth over a 0.01 seconds

typedef struct sdcmotorBoardDriver {
	TIM_HandleTypeDef * tim1[NUM_DCMOTORS];
	TIM_HandleTypeDef * tim2[NUM_DCMOTORS];
	uint32_t channel1[NUM_DCMOTORS];
	uint32_t channel2[NUM_DCMOTORS];
    uint16_t encoderPin1[NUM_DCMOTORS];
    uint16_t encoderPin2[NUM_DCMOTORS];

    bool coastMode[NUM_DCMOTORS]; // Coast when velocity is 0 (vs braking)
    uint32_t acceleration[NUM_DCMOTORS]; // change in the percentage of max power per msec
    uint32_t deceleration[NUM_DCMOTORS];
    int32_t desiredVoltagePercent[NUM_DCMOTORS];
    int32_t VoltagePercent[NUM_DCMOTORS];
    bool brake[NUM_DCMOTORS];
    uint32_t encoderCount[NUM_DCMOTORS];
    uint32_t counts[NUM_DCMOTORS][RPM_SMOOTHING_BUFFER];
    uint32_t rpm[NUM_DCMOTORS];
} dcmotorBoardDriver;

uint16_t adcReadings[NUM_DCMOTORS] = {0};

dcmotorBoardDriver dcmDrv = {0};

const fp::FieldEntry dcmotorsFields[] = {
    {&dcmDrv.desiredVoltagePercent[0],   "powers",          NUM_DCMOTORS,  fp::FieldDataType::Int,      4,  fp::FieldFlags::Gettable | fp::FieldFlags::Settable, NULL, NULL, "percentage"},
    {&dcmDrv.brake[0],                   "braking",        NUM_DCMOTORS,  fp::FieldDataType::Boolean,  1,  fp::FieldFlags::Gettable | fp::FieldFlags::Settable, NULL, NULL, NULL},
    {&dcmDrv.acceleration[0],            "accelerations",   NUM_DCMOTORS,  fp::FieldDataType::Uint,     4,  fp::FieldFlags::Gettable | fp::FieldFlags::Settable, NULL, NULL, NULL},
    {&dcmDrv.deceleration[0],            "decelerations",   NUM_DCMOTORS,  fp::FieldDataType::Uint,     4,  fp::FieldFlags::Gettable | fp::FieldFlags::Settable, NULL, NULL, NULL},
    {&dcmDrv.coastMode[0],               "coast_modes",     NUM_DCMOTORS,  fp::FieldDataType::Boolean,  1,  fp::FieldFlags::Gettable | fp::FieldFlags::Settable, NULL, NULL, NULL},
    {&dcmDrv.rpm,                        "rpms",            NUM_DCMOTORS,  fp::FieldDataType::Uint,     4,  fp::FieldFlags::Gettable           , NULL, NULL, NULL},
    // {&dcmDrv.VoltagePercent[0],    "actual_velocity",    DCMOTOR_FIELDS_OFFSET + 10,   1,  fp::FieldDataType::Int,   4,  0, &dcmotorNonSettableFieldMetaData, NULL, NULL},
    // {&dcmDrv.current[0],           "current",              DCMOTOR_FIELDS_OFFSET + 12,  1,  fp::FieldDataType::Uint,  4,  0, &dcmotorNonSettableFieldMetaData, NULL, NULL},
};
const fp::FieldTable dcmotorsFieldTable = {
    .fields = (fp::FieldEntry*) dcmotorsFields,
    .numFields = sizeof(dcmotorsFields)/sizeof(fp::FieldEntry)
};


// ======================== //

void updateMotorVelocity(uint32_t i) {
    int32_t diff = dcmDrv.desiredVoltagePercent[i] - dcmDrv.VoltagePercent[i];
    if (diff > 0) {
        // Accelerate
        if (dcmDrv.acceleration[i] > diff) {
            dcmDrv.VoltagePercent[i] = dcmDrv.desiredVoltagePercent[i];
        } else {
            dcmDrv.VoltagePercent[i] += dcmDrv.acceleration[i];
        }
    } else if (diff < 0) {
        // Decelerate
        diff = -diff;
        if (dcmDrv.deceleration[i] > diff) {
            dcmDrv.VoltagePercent[i] = dcmDrv.desiredVoltagePercent[i];
        } else {
            dcmDrv.VoltagePercent[i] -= dcmDrv.deceleration[i];
        }
    }
}

void setCompare(TIM_HandleTypeDef * tim, uint32_t channel, uint32_t period) {
    softAssert(period <= COUNTER_PERIOD, "Invalid motor PWM period");
    __HAL_TIM_SET_COMPARE(tim, channel, period);
}

void setPWM(uint32_t i) {
    if (dcmDrv.brake[i]) {
        // Brake
        dcmDrv.VoltagePercent[i] = 0;
        dcmDrv.desiredVoltagePercent[i] = 0;
        setCompare(dcmDrv.tim1[i], dcmDrv.channel1[i], COUNTER_PERIOD);
        setCompare(dcmDrv.tim2[i], dcmDrv.channel2[i], COUNTER_PERIOD);
    } else {
        if (dcmDrv.VoltagePercent[i] > 0) {
            // Forwards
            // Data sheet suggests braking whilst not driving
            uint32_t period = COUNTER_PERIOD - (dcmDrv.VoltagePercent[i] * 24);
            setCompare(dcmDrv.tim1[i], dcmDrv.channel1[i], COUNTER_PERIOD);
            setCompare(dcmDrv.tim2[i], dcmDrv.channel2[i], period);
        } else if (dcmDrv.VoltagePercent[i] < 0) {
            // Reverse
            // Data sheet suggests braking whilst not driving
            uint32_t period = COUNTER_PERIOD + (dcmDrv.VoltagePercent[i] * 24);
            setCompare(dcmDrv.tim1[i], dcmDrv.channel1[i], period);
            setCompare(dcmDrv.tim2[i], dcmDrv.channel2[i], COUNTER_PERIOD);
        } else {
            if (dcmDrv.coastMode[i]) {
                // Coast
                setCompare(dcmDrv.tim1[i], dcmDrv.channel1[i], 0);
                setCompare(dcmDrv.tim2[i], dcmDrv.channel2[i], 0);
            } else {
                // Brake
                setCompare(dcmDrv.tim1[i], dcmDrv.channel1[i], COUNTER_PERIOD);
                setCompare(dcmDrv.tim2[i], dcmDrv.channel2[i], COUNTER_PERIOD);
           }
        }
    }
}

void motor_GPIO_EXTI_Rising_Callback(uint16_t GPIO_Pin) {
    for (uint32_t i=0; i<NUM_DCMOTORS; i++) {
        if (GPIO_Pin == dcmDrv.encoderPin1[i] || GPIO_Pin == dcmDrv.encoderPin2[i]) {
            dcmDrv.encoderCount[i]++;
        }
    }
}

void readRpms() {
    for (uint32_t i=0; i<NUM_DCMOTORS; i++) {
        // Shift down
        for (uint32_t j=0; j<RPM_SMOOTHING_BUFFER-1; j++) {
            dcmDrv.counts[i][j] = dcmDrv.counts[i][j+1];
        }

        dcmDrv.counts[i][RPM_SMOOTHING_BUFFER-1] = dcmDrv.encoderCount[i];
        dcmDrv.encoderCount[i] = 0;

        uint32_t sum = 0;
        for (uint32_t j=0; j<RPM_SMOOTHING_BUFFER; j++) {
            sum += dcmDrv.counts[i][j];
        }
        dcmDrv.rpm[i] = (60 * 1000 * sum) / (100 * 6); // Called every 10ms and smoothed over 10 samples, with 3 magnets and 2 sensors (so 6 counts per rotation)

    }

}

void updateMotor() {
    for (uint32_t i=0; i<NUM_DCMOTORS; i++) {
        updateMotorVelocity(i);
        setPWM(i);
    }
}

// void readCurrent() {
//     for (uint32_t i=0; i<NUM_DCMOTORS; i++) {
//         current[i] = adcReadings
//         adcReadings
//         HAL_ADC_Start_DMA(joystick.adc, (uint32_t *) adcDmaData, 2); // Start next conversion
//     }
// }



void initDCMotors(TIM_HandleTypeDef * tim1s[], uint32_t tim1Channels[], TIM_HandleTypeDef * tim2s[], uint32_t tim2Channels[], uint16_t encoderPin1[NUM_DCMOTORS], uint16_t encoderPin2[NUM_DCMOTORS]) {
    for (uint8_t i=0; i<NUM_DCMOTORS; i++) {
        dcmDrv.coastMode[i] = 1;
        dcmDrv.deceleration[i] = 1; // Percentage change per msec - 100ms to fully ramp down
        dcmDrv.acceleration[i] = 1; // Percentage change per msec - 100ms to fully ramp up
        dcmDrv.desiredVoltagePercent[i] = 0;
        

        dcmDrv.desiredVoltagePercent[i] = 20;

        dcmDrv.tim1[i] = tim1s[i];
        dcmDrv.channel1[i] = tim1Channels[i];
        dcmDrv.tim2[i] = tim2s[i];
        dcmDrv.channel2[i] = tim2Channels[i];

        dcmDrv.encoderPin1[i] = encoderPin1[i];
        dcmDrv.encoderPin2[i] = encoderPin2[i];

        softAssert((HAL_TIM_PWM_Start(tim1s[i], tim1Channels[i]) == HAL_OK), "Failed to start PWM");
        softAssert((HAL_TIM_PWM_Start(tim2s[i], tim2Channels[i]) == HAL_OK), "Failed to start PWM");
    }
}

void loopDCMotors(uint32_t ticks) {
    static uint32_t nextTick = 0;
    if (nextTick <= ticks) {
        nextTick = ticks + 2; // Update every 2ms
        updateMotor();
    }

    static uint32_t nextTickRpms = 0;
    if (nextTickRpms <= ticks) {
        nextTickRpms = ticks + 10; // Update every 10ms
        readRpms();
    }
}

#endif //ENABLE_SERVO_MODULE
