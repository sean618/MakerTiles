#include "project.h"

#ifdef ENABLE_SPEAKER_MODULE

#include <stdio.h>
#include <math.h>
#include <string.h>
#include "stm32/stm32Node.h"
#include "useful.h"  // QUEUE_* circular-buffer macros

// HW setup:
// - I2S  -16kHz, 16 bits
// - PWM - 48MHz, 16 bits, (only use 10 bits (1000), x3 for smoothing, x16000 for audio rate => 48MHz)
// - counter period 999

// #define NUM_PWM_BITS 8
#define MAX_PWM_VAL 250
#define NUM_SAMPLES 160 // Fills a field packet
#define PWM_REPEATED_SAMPLES 1
#define PWM_DMA_BUFFER_LENGTH (3 * PWM_REPEATED_SAMPLES * (NUM_SAMPLES/2)) // uint16_t - callback every 40 samples, at 16kHz => 2.5ms
#define I2S_DMA_BUFFER_LENGTH ((NUM_SAMPLES/2)) // uint16_t - callback every 40 samples, at 16kHz => 2.5ms
#define DATA_BUFFER_SIZE 2900

typedef struct {
    // TIM_HandleTypeDef * tim;
    // TIM_HandleTypeDef * dacTim;
    // DAC_HandleTypeDef * dac;
    // uint32_t timChannel;
    // uint32_t dacChannel;

    I2S_HandleTypeDef * i2s;
    GPIO_TypeDef* i2sShutDownGpio;
    uint16_t i2sShutDownGpioPin;
    // GPIO_TypeDef* pwmShutDownGpio;
    // uint16_t pwmShutDownGpioPin;

    bool useI2s;
    bool useDac;
    bool run;
    uint8_t frequency16kDivider;
    
    uint32_t dataBufferStart;
    uint32_t dataBufferEnd;
    int8_t dataBuffer[DATA_BUFFER_SIZE];

    int16_t i2sDmaBuffer[I2S_DMA_BUFFER_LENGTH];
    uint16_t pwmDmaBuffer[PWM_DMA_BUFFER_LENGTH];

    uint64_t overflows;
    uint64_t underflows;
    bool underflowStarted;
    uint16_t volume;


    int16_t lastVal;
} sSpeakerBoardDriver;


sSpeakerBoardDriver speakerDrv = {0};

void startAudioMode(bool useI2s);

bool setI2sField(fp::FieldEntry * field, fp::FieldIndex fieldOffset, fp::FieldIndex numFields, uint8_t * data) {
    bool useI2s = *data;
    if (useI2s != speakerDrv.useI2s) {
        startAudioMode(useI2s);
    }
    return true;
}

bool setBufferField(fp::FieldEntry * field, fp::FieldIndex fieldOffset, fp::FieldIndex numFields, uint8_t * data) {
    // fpAssert(numFields == NUM_SAMPLES, "");
    volatile uint32_t length = QUEUE_LENGTH(speakerDrv.dataBufferStart, speakerDrv.dataBufferEnd, DATA_BUFFER_SIZE);

    if (length + numFields >= DATA_BUFFER_SIZE) {
    	// TODO: make this return false
        // speakerDrv.overflows += numFields;
        // return true;
        return false;
    }

    // Slow - could be improved
    for (uint32_t i=0; i<numFields; i++) {
        QUEUE_APPEND(speakerDrv.dataBuffer, speakerDrv.dataBufferStart, speakerDrv.dataBufferEnd, DATA_BUFFER_SIZE, data[i]);
    }
    return true;
}

uint8_t * getBufferLevel(fp::FieldEntry * field, fp::FieldIndex fieldOffset, fp::FieldIndex numFields) {
    static uint16_t length;
    length = QUEUE_LENGTH(speakerDrv.dataBufferStart, speakerDrv.dataBufferEnd, DATA_BUFFER_SIZE);
    return (uint8_t *)&length;
}

fp::FieldEntry speakerFields[] = {
    {&speakerDrv.useI2s,               "use_i2s",                   1,                 fp::FieldDataType::Boolean,   1,  fp::FieldFlags::Gettable | fp::FieldFlags::Settable,  setI2sField,    NULL,           NULL},
    {&speakerDrv.run,                  "run",                       1,                 fp::FieldDataType::Boolean,   1,  fp::FieldFlags::Gettable | fp::FieldFlags::Settable,  NULL,           NULL,           NULL},
    {&speakerDrv.frequency16kDivider,  "frequency_16k_divider",     1,                 fp::FieldDataType::Uint,      1,  fp::FieldFlags::Gettable | fp::FieldFlags::Settable,  NULL,           NULL,           NULL},
    {NULL,                             "buffer",                    NUM_SAMPLES,       fp::FieldDataType::Int,       1,  fp::FieldFlags::Settable,             setBufferField, NULL,           NULL},
    {NULL,                             "buffer_level",              1,                 fp::FieldDataType::Uint,      2,  fp::FieldFlags::Gettable,             NULL,           getBufferLevel, NULL},
    {&speakerDrv.underflows,           "underflows",                1,                 fp::FieldDataType::Uint,      8,  fp::FieldFlags::Gettable | fp::FieldFlags::Settable,  NULL,           NULL,           NULL},
    {&speakerDrv.overflows,            "overflows",                 1,                 fp::FieldDataType::Uint,      8,  fp::FieldFlags::Gettable | fp::FieldFlags::Settable,  NULL,           NULL,           NULL},
    {&speakerDrv.volume,               "volume",                    1,                 fp::FieldDataType::Uint,      2,  fp::FieldFlags::Gettable | fp::FieldFlags::Settable,  NULL,           NULL,           NULL},
    {&speakerDrv.lastVal,              "lst_val",                   1,                 fp::FieldDataType::Int,       2,  fp::FieldFlags::Gettable | fp::FieldFlags::Settable,  NULL,           NULL,           NULL},

};

const fp::FieldTable speakerFieldTable = {
    .fields = (fp::FieldEntry*) speakerFields,
    .numFields = sizeof(speakerFields)/sizeof(fp::FieldEntry)
};


int8_t sbTestData[32] = {0,50,0,-50,0,50,0,-50,    0,50,0,-50,0,50,0,-50,    0,50,0,-50,0,50,0,-50,   0,50,0,-50,0,50,0,-50};
uint32_t sbIdx = 0;
// bool runStarted = false;
// uint32_t badCount = 0;

void fillDmaBuffer(bool fillFirstHalfOfDma) {

    if (!speakerDrv.run) {
        return;
    }

    uint32_t numRepeatedSamples = speakerDrv.frequency16kDivider;
    uint32_t numSamplesToCpy;
    uint16_t * buffer;
    if (speakerDrv.useI2s) {
        buffer = (uint16_t *) (fillFirstHalfOfDma ? &speakerDrv.i2sDmaBuffer[0] : &speakerDrv.i2sDmaBuffer[I2S_DMA_BUFFER_LENGTH/2]);
        numSamplesToCpy = I2S_DMA_BUFFER_LENGTH/2;
    } else {
        buffer = fillFirstHalfOfDma ? &speakerDrv.pwmDmaBuffer[0] : &speakerDrv.pwmDmaBuffer[PWM_DMA_BUFFER_LENGTH/2];
        numRepeatedSamples *= 3 * PWM_REPEATED_SAMPLES; // for smoothing (and for dma ch1,2,3)
        numSamplesToCpy = PWM_DMA_BUFFER_LENGTH/2;
    }

    uint32_t idx = 0;

    while (1) {
        // Copy each sample 3x - as we repeat each pwm sample 3x to help smooth it out

        int16_t val = (speakerDrv.lastVal * 120)/ 128; // Decay to zero


        if (QUEUE_EMPTY(speakerDrv.dataBufferStart, speakerDrv.dataBufferEnd, DATA_BUFFER_SIZE)) {
            if (speakerDrv.underflowStarted == false) {
                speakerDrv.underflows++;
                speakerDrv.underflowStarted = true;
            }
        } else {
            speakerDrv.underflowStarted = false;

            val = speakerDrv.dataBuffer[speakerDrv.dataBufferStart];

            // val = sbTestData[sbIdx];
            // sbIdx++;
            // sbIdx = sbIdx % 32;

            // if (runStarted == false ) {
            //     if (val == 50) {
            //         runStarted = true;
            //         sbIdx = 1;
            //     }
            // } else {
            //     sbIdx++;
            //     if (sbIdx >= 4) {
            //         sbIdx = 0;
            //     }
            //     int8_t expVal = sbTestData[sbIdx];
            //     if (expVal != val) {
            //         runStarted = false;
            //         badCount++;
            //     }
            // }

            speakerDrv.dataBufferStart++;
            if (speakerDrv.dataBufferStart >= DATA_BUFFER_SIZE) {
                speakerDrv.dataBufferStart = 0;
            }

            if (speakerDrv.useDac) {
                val += (1 << 12)/2; // (1 << (NUM_PWM_BITS-1)); // Midpoint
                val = (val * 4 * speakerDrv.volume)/256; // 8 bit to 12 bit

            } else if (speakerDrv.useI2s) {
                val = (val * 256 * speakerDrv.volume)/256; // 8 bit to 16 bit
            } else {
                val = (val * speakerDrv.volume)/256; // 8 bit to 10 bit
                
                val += MAX_PWM_VAL/2; // (1 << (NUM_PWM_BITS-1)); // Midpoint
                myAssert(val < MAX_PWM_VAL, "");
            }
        }

        for (uint8_t j=0; j<numRepeatedSamples; j++) {
            buffer[idx] = val;
            speakerDrv.lastVal = val;
            idx++;
        }
        if (idx >= numSamplesToCpy) {
            break;
        }
    }
}

// void HAL_TIM_PWM_PulseFinishedHalfCpltCallback(TIM_HandleTypeDef *htim) {
//     fillDmaBuffer(true);
// }
// void HAL_TIM_PWM_PulseFinishedCallback(TIM_HandleTypeDef *htim) {
//     fillDmaBuffer(false);
// }

void HAL_I2S_TxHalfCpltCallback(I2S_HandleTypeDef *hi2s) {
    fillDmaBuffer(true);
}
void HAL_I2S_TxCpltCallback(I2S_HandleTypeDef *hi2s) {
    fillDmaBuffer(false);
}

// void HAL_DACEx_ConvHalfCpltCallbackCh2(DAC_HandleTypeDef* hdac) {
//     fillDmaBuffer(true);
// }

// void HAL_DACEx_ConvCpltCallbackCh2(DAC_HandleTypeDef* hdac) {
//     fillDmaBuffer(false);
// }



void startAudioMode(bool useI2s) {
    bool previousUseI2s = speakerDrv.useI2s;
    if (previousUseI2s) {
        myAssert(HAL_OK == HAL_I2S_DMAStop(speakerDrv.i2s), "");
    } else {
        // myAssert(HAL_OK == HAL_TIM_PWM_Stop_DMA(speakerDrv.tim, speakerDrv.timChannel), "");
    }
    
    speakerDrv.useI2s = useI2s;

    fillDmaBuffer(true);
    fillDmaBuffer(false);

    if (useI2s) {
//        HAL_GPIO_WritePin(speakerDrv.pwmShutDownGpio, speakerDrv.pwmShutDownGpioPin, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(speakerDrv.i2sShutDownGpio, speakerDrv.i2sShutDownGpioPin, GPIO_PIN_SET);
        myAssert(HAL_OK == HAL_I2S_Transmit_DMA(speakerDrv.i2s, (uint16_t *)speakerDrv.i2sDmaBuffer, I2S_DMA_BUFFER_LENGTH), "");
    } else {
        // HAL_GPIO_WritePin(speakerDrv.i2sShutDownGpio, speakerDrv.i2sShutDownGpioPin, GPIO_PIN_RESET);
        // HAL_GPIO_WritePin(speakerDrv.pwmShutDownGpio, speakerDrv.pwmShutDownGpioPin, GPIO_PIN_SET);

        // myAssert(HAL_OK == HAL_TIM_PWM_Start_DMA(speakerDrv.tim, speakerDrv.timChannel, (uint32_t *) speakerDrv.pwmDmaBuffer, PWM_DMA_BUFFER_LENGTH), "");
        
        // myAssert(HAL_OK == HAL_TIM_DMABurst_WriteStart(speakerDrv.tim, TIM_DMABASE_CCR1, TIM_DMA_UPDATE, (uint32_t *) speakerDrv.pwmDmaBuffer, PWM_DMA_BUFFER_LENGTH), "");
    }
}

// void startDACMode() {
//     HAL_GPIO_WritePin(GPIOB, GPIO_PIN_10, GPIO_PIN_SET);

//     // speakerDrv.useDac = true;
//     speakerDrv.useI2s = true;
//     fillDmaBuffer(true);
//     fillDmaBuffer(false);


//     myAssert(HAL_OK == HAL_DAC_Start_DMA(speakerDrv.dac, speakerDrv.dacChannel, (uint32_t *)speakerDrv.i2sDmaBuffer, I2S_DMA_BUFFER_LENGTH, DAC_ALIGN_12B_R), "");
//     // HAL_StatusTypeDef HAL_DAC_Start_DMA(DAC_HandleTypeDef* hdac, uint32_t Channel, uint32_t* pData, uint32_t Length, uint32_t Alignment);

//     myAssert(HAL_OK == HAL_TIM_Base_Start(speakerDrv.dacTim), "");
// }

// ======================== //

void initSpeaker(
        // TIM_HandleTypeDef * pwmTim,
        // uint32_t pwmTimChannel,
        I2S_HandleTypeDef * i2s,
        GPIO_TypeDef* i2sShutDownGpio,
        uint16_t i2sShutDownGpioPin
        // GPIO_TypeDef* pwmShutDownGpio,
        // uint16_t pwmShutDownGpioPin,
        // DAC_HandleTypeDef *hdac,
        //  uint32_t dacChannel,
        // TIM_HandleTypeDef * dacTim
    ) {

    
    // speakerDrv.tim                 = pwmTim;
    // speakerDrv.timChannel          = pwmTimChannel;
    speakerDrv.i2s                 = i2s;
    speakerDrv.i2sShutDownGpio     = i2sShutDownGpio;
    speakerDrv.i2sShutDownGpioPin  = i2sShutDownGpioPin;
    // speakerDrv.pwmShutDownGpio     = pwmShutDownGpio;
    // speakerDrv.pwmShutDownGpioPin  = pwmShutDownGpioPin;
    // speakerDrv.dac              = hdac;
    // speakerDrv.dacChannel          = dacChannel;
    // speakerDrv.dacTim          = dacTim;

    // // DAC shutdown
    // HAL_GPIO_WritePin(GPIOB, GPIO_PIN_10, GPIO_PIN_RESET);
    // HAL_GPIO_WritePin(speakerDrv.pwmShutDownGpio, speakerDrv.pwmShutDownGpioPin, GPIO_PIN_RESET);
    // HAL_GPIO_WritePin(speakerDrv.i2sShutDownGpio, speakerDrv.i2sShutDownGpioPin, GPIO_PIN_RESET);
    // // PWM channel 2 - set to zero
    // HAL_GPIO_WritePin(GPIOA, GPIO_PIN_9, GPIO_PIN_RESET);

    // // DAC channel 1 - set to zero
    // // HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_RESET);
    // myAssert(HAL_OK == HAL_DAC_SetValue(hdac, dacChannel, DAC_ALIGN_12B_R, (1 << 11)), "");
    // myAssert(HAL_OK == HAL_DAC_Start(hdac, dacChannel), "");



    startAudioMode(true);
    // startAudioMode(false);
    // startDACMode();
    speakerDrv.frequency16kDivider = 2;
    speakerDrv.run = 1;
    speakerDrv.volume = 64;
}


void loopSpeaker(uint32_t ticks) {
    static bool levelSent = 0;
    // Every time the circular buffer goes below half publish the level
    if (QUEUE_LENGTH(speakerDrv.dataBufferStart, speakerDrv.dataBufferEnd, DATA_BUFFER_SIZE) <= DATA_BUFFER_SIZE/2) {
        if (!levelSent) {
            levelSent = true;
            // Send buffer_level
            protocolSendFields(4, 1);
        }
    } else {
        // When it goes back over then clear the sent flag
        levelSent = false;
    }
}

 #endif
