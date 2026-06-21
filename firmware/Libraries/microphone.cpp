#include "project.h"

#ifdef ENABLE_MICROPHONE_MODULE

#include <stdio.h>
#include <math.h>
#include <string.h>
#include "stm32/stm32Node.h"

// HW setup:
//   - Timer set to have a freq of 32KHz
//   - ADC has a circular dma setup
//   - Adc is triggered of the timer

#define NUM_12B_SAMPLES 80
#define NUM_8B_SAMPLES  (NUM_12B_SAMPLES*2)
#define ADC_BUFFER_SIZE (NUM_12B_SAMPLES*2)

#define NUM_TAPS 16

#define MAX_SB_SAMPLES 2000


typedef struct {
    int16_t sample[NUM_TAPS];
    uint16_t index;
} tFirState;

typedef struct {
    // GPIO_TypeDef * gpioSection[NUM_microphoneS];
    // uint16_t gpioPin[NUM_microphoneS];
    // uint8_t pressed[NUM_microphoneS];
    // uint32_t pressedCount[NUM_microphoneS];
    // bool pulledUp;
    TIM_HandleTypeDef * tim;
    ADC_HandleTypeDef * adc;
    uint8_t frequency4kMulti;
    bool sample12bNot8b;
    uint16_t adcBuffer[ADC_BUFFER_SIZE];
    bool recording;

    uint32_t sampleIndex;
    tFirState fir32k;
    tFirState fir16k;
    tFirState fir8k;
    tFirState fir4k;

    uint32_t outgoingIndex;
    uint8_t outgoing8bBuffer[NUM_8B_SAMPLES];
    uint16_t outgoing12bBuffer[NUM_12B_SAMPLES];
    uint16_t samples16b[NUM_12B_SAMPLES];
    uint8_t samples8b[NUM_8B_SAMPLES];

    uint16_t peakLoudness;
    uint64_t averageLoudnessSumSquares;
    uint16_t averageLoudnessNumSamples;

    // uint8_t sbSamples[MAX_SB_SAMPLES];
    // uint32_t sbSamplesIndex;


    int64_t sum;
    int64_t fullSum;
    uint64_t numInSum;
    int16_t dcOffset;

    uint32_t txCount;
} sMicrophoneBoardDriver;


sMicrophoneBoardDriver microphoneDrv = {0};

// Just zero it after read
uint8_t * getPeakLoudness(fp::FieldEntry * field, fp::FieldIndex fieldOffset, fp::FieldIndex numFields) {
    static uint16_t storedPeak;
    storedPeak = microphoneDrv.peakLoudness;
    microphoneDrv.peakLoudness = 0;
    return (uint8_t *) &storedPeak;
}

// Just zero it after read
uint8_t * getAverageLoudness(fp::FieldEntry * field, fp::FieldIndex fieldOffset, fp::FieldIndex numFields) {
    static uint16_t storedAverage;
    uint64_t tmp = microphoneDrv.averageLoudnessSumSquares / microphoneDrv.averageLoudnessNumSamples;
    storedAverage = tmp;
    microphoneDrv.averageLoudnessNumSamples = 0;
    microphoneDrv.averageLoudnessSumSquares = 0;
    return (uint8_t *) &storedAverage;
}

fp::FieldEntry microphoneFields[] = {
    {&microphoneDrv.recording,         "full_recording",            1,                 fp::FieldDataType::Boolean,   1,  fp::FieldFlags::Gettable | fp::FieldFlags::Settable, NULL, NULL, NULL},
    {&microphoneDrv.frequency4kMulti,  "frequency_4k_multiplier",   1,                 fp::FieldDataType::Uint,      1,  fp::FieldFlags::Gettable | fp::FieldFlags::Settable, NULL, NULL, NULL},
    {&microphoneDrv.sample12bNot8b,    "12b_not_8b",                1,                 fp::FieldDataType::Boolean,   1,  fp::FieldFlags::Gettable | fp::FieldFlags::Settable, NULL, NULL, NULL},
    {&microphoneDrv.samples16b[0],     "samples_12b",               NUM_12B_SAMPLES,   fp::FieldDataType::Int,       2,  fp::FieldFlags::Gettable | fp::FieldFlags::Streaming, NULL, NULL, NULL},
    {&microphoneDrv.samples8b[0],      "samples_8b",                NUM_8B_SAMPLES,    fp::FieldDataType::Int,       1,  fp::FieldFlags::Gettable | fp::FieldFlags::Streaming, NULL, NULL, NULL},
    {NULL,                             "peak_loudness",              1,                 fp::FieldDataType::Uint,       2,  fp::FieldFlags::Gettable, NULL, &getPeakLoudness, NULL},
    {NULL ,                            "average_loudness",           1,                 fp::FieldDataType::Uint,       2,  fp::FieldFlags::Gettable, NULL, &getAverageLoudness, NULL},
};
const fp::FieldTable microphoneFieldTable = {
    .fields = (fp::FieldEntry*) microphoneFields,
    .numFields = sizeof(microphoneFields)/sizeof(fp::FieldEntry)
};



static const int16_t firCoeffs[NUM_TAPS] = { -86, 91, 422, -91, -1747, -934, 5604, 13125, 13125, 5604, -934, -1747, -91, 422, 91, -86 };

static inline void recordForFir(int16_t newSample, tFirState * fir) {
    fir->sample[fir->index] = newSample;

    // Update circular buffer index
    fir->index++;
    if (fir->index >= NUM_TAPS) {
        fir->index = 0;
    }
}

static int16_t downsampleUsingFIR(tFirState * fir) {
    int32_t acc = 0;
    // Get Previous idx
    uint16_t idx = fir->index;
    // Circular buffer
    for (int i = 0; i < NUM_TAPS; i++) {
        if (idx == 0) {
            idx = NUM_TAPS - 1;
        } else {
            idx--;
        }
        acc += (int32_t)firCoeffs[i] * fir->sample[idx];
    }
    return (int16_t)(acc >> 15); // Dividing by 32768 which is what the FIR taps sum to
}

void addSampleToOutgoingBuffer(int16_t sample) {
    if (!microphoneDrv.recording) {
        return;
    }
    if (microphoneDrv.sample12bNot8b) {
        microphoneDrv.outgoing12bBuffer[microphoneDrv.outgoingIndex] = sample;
        microphoneDrv.outgoingIndex++;

        if (microphoneDrv.outgoingIndex >= NUM_12B_SAMPLES) {
            microphoneDrv.outgoingIndex = 0;
            memcpy(microphoneDrv.samples16b, microphoneDrv.outgoing12bBuffer, NUM_12B_SAMPLES*2);
            microphoneDrv.txCount++;
            protocolSendFields(3, NUM_12B_SAMPLES);
        }
    } else {
        microphoneDrv.outgoing8bBuffer[microphoneDrv.outgoingIndex] = sample;
        microphoneDrv.outgoingIndex++;

        if (microphoneDrv.outgoingIndex >= NUM_8B_SAMPLES) {
            microphoneDrv.outgoingIndex = 0;
            for (uint32_t i=0; i<NUM_8B_SAMPLES; i++) {
                microphoneDrv.samples8b[i] = microphoneDrv.outgoing8bBuffer[i] >> 4;
                microphoneDrv.txCount++;
                protocolSendFields(4, NUM_8B_SAMPLES);
            }
        }
    }
} 

void processAdcSamples(const uint16_t * input) {

    for (uint32_t i=0; i<ADC_BUFFER_SIZE/2; i++) {
        microphoneDrv.sampleIndex++;

       microphoneDrv.fullSum += input[i];
       microphoneDrv.sum += input[i];
       microphoneDrv.numInSum++;

       // Every 4000 samples update the DC offset
       if (microphoneDrv.numInSum % (1 << 12) == 0) {
           microphoneDrv.dcOffset = (microphoneDrv.sum >> 12);
           microphoneDrv.sum = 0;
       }

        // int16_t signedSample = (int16_t)(input[i] - 664); // 664 // microphone dc bias should 1.1v
        int16_t signedSample = (int16_t)(input[i]);

        // microphoneDrv.sbSamples[microphoneDrv.sbSamplesIndex] = input[i] & 0xFF;
        // microphoneDrv.sbSamplesIndex++;
        // if (microphoneDrv.sbSamplesIndex >= MAX_SB_SAMPLES) {
        //     microphoneDrv.sbSamplesIndex = 0;
        // }



		int16_t absVal = signedSample >= 0 ? signedSample : -signedSample;
		if (absVal > microphoneDrv.peakLoudness) {
			microphoneDrv.peakLoudness = absVal;
		}

		microphoneDrv.averageLoudnessSumSquares += absVal * absVal;
		microphoneDrv.averageLoudnessNumSamples++;



        // 32k
        if (microphoneDrv.frequency4kMulti == 8) {
            addSampleToOutgoingBuffer(signedSample);
        }
        recordForFir(signedSample, &microphoneDrv.fir32k);

        if ((microphoneDrv.sampleIndex % 2) == 0) {
            int16_t filtered = downsampleUsingFIR(&microphoneDrv.fir32k);
            recordForFir(filtered, &microphoneDrv.fir16k);

            // 16k
            if (microphoneDrv.frequency4kMulti == 4) {
                addSampleToOutgoingBuffer(filtered);
            }

            if ((microphoneDrv.sampleIndex % 4) == 0) {
                filtered = downsampleUsingFIR(&microphoneDrv.fir16k);
                recordForFir(filtered, &microphoneDrv.fir8k);
                
                // 8k
                if (microphoneDrv.frequency4kMulti == 2) {
                    addSampleToOutgoingBuffer(filtered);
                }

                if ((microphoneDrv.sampleIndex % 8) == 0) {
                	microphoneDrv.sampleIndex = 0;

                    filtered = downsampleUsingFIR(&microphoneDrv.fir8k);
                    // 4k
                    if (microphoneDrv.frequency4kMulti == 1) {
                        addSampleToOutgoingBuffer(filtered);
                    }
                    // filtered = downsampleUsingFIR(&microphoneDrv.fir4k);
                }
            }
        }
    }
}

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc) {
    processAdcSamples(&microphoneDrv.adcBuffer[ADC_BUFFER_SIZE/2]);
}
void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef *hadc) {
    processAdcSamples(&microphoneDrv.adcBuffer[0]);
}
void HAL_ADC_ErrorCallback(ADC_HandleTypeDef *hadc) {
    myAssert(0, "");
}

// ======================== //

void initMicrophone(TIM_HandleTypeDef * tim, ADC_HandleTypeDef * adc) {
    microphoneDrv.adc = adc;
    microphoneDrv.tim = tim;
    microphoneDrv.sample12bNot8b = 1;
    microphoneDrv.frequency4kMulti = 4; // 16kHz 

//    microphoneDrv.recording = 1;

//   uint32_t start = HAL_GetTick();
//   for (uint32_t i=0; i<1000; i++) {
//       processAdcSamples(&microphoneDrv.adcBuffer[0]);
//   }
//   volatile uint32_t diff = HAL_GetTick() - start;


    myAssert(HAL_OK == HAL_ADC_Start_DMA(adc, (uint32_t *) microphoneDrv.adcBuffer, ADC_BUFFER_SIZE), "");
    myAssert(HAL_OK == HAL_TIM_Base_Start_IT(tim), "");

}


void loopMicrophone(uint32_t ticks) {
    
}

 #endif
