#include "project.h"
#ifdef ENABLE_BATTERY_MODULE

#include <stdio.h>
#include <math.h>
#include <string.h>
#include "battery.h"

// HW setup:
// Configure Pull-up on stby and charge

typedef struct {
    GPIO_TypeDef * standbyGpio;
    uint16_t standbyPin;
    GPIO_TypeDef * chargingGpio;
    uint16_t chargingPin;

    // GPIO_TypeDef * voltageGpio;
    // uint16_t voltagePin;
    // GPIO_TypeDef * currentGpio;
    // uint16_t currentPin;

    GPIO_TypeDef * enable5vGpio;
    uint16_t enable5vPin;

    ADC_HandleTypeDef *adc;

    bool standby;
    bool charging;
    uint32_t voltagemv;
    uint32_t currentma;
    uint32_t chargePercent;
} sBatteryBoardDriver;

sBatteryBoardDriver batteryDrv = {0};


const fp::FieldEntry batteryFields[] = {
    {&batteryDrv.standby,       "charged",       1, fp::FieldDataType::Boolean,   1,  fp::FieldFlags::Gettable, NULL, NULL, NULL},
    {&batteryDrv.charging,      "charging",      1, fp::FieldDataType::Boolean,   1,  fp::FieldFlags::Gettable, NULL, NULL, NULL},
    {&batteryDrv.voltagemv,     "voltagemv",     1, fp::FieldDataType::Uint,      4,  fp::FieldFlags::Gettable, NULL, NULL, "mV"}, 
    {&batteryDrv.currentma,     "currentma",     1, fp::FieldDataType::Uint,      4,  fp::FieldFlags::Gettable, NULL, NULL, "mA"}, 
    {&batteryDrv.chargePercent, "level",         1, fp::FieldDataType::Uint,      4,  fp::FieldFlags::Gettable, NULL, NULL, "percent"},
};
const fp::FieldTable batteryFieldTable = {
    .fields = (fp::FieldEntry*) batteryFields,
    .numFields = sizeof(batteryFields)/sizeof(fp::FieldEntry)
};

void initialiseBattery(
        GPIO_TypeDef * standbyGpio,
        uint16_t standbyPin,
        GPIO_TypeDef * chargingGpio,
        uint16_t chargingPin,
        GPIO_TypeDef * enable5vGpio,
        uint16_t enable5vPin,
        ADC_HandleTypeDef *adc) {
    batteryDrv.standbyGpio = standbyGpio;
    batteryDrv.standbyPin = standbyPin;
    batteryDrv.chargingGpio = chargingGpio;
    batteryDrv.chargingPin = chargingPin;
    batteryDrv.enable5vGpio = enable5vGpio;
    batteryDrv.enable5vPin = enable5vPin;
    batteryDrv.adc = adc;
    HAL_GPIO_WritePin(batteryDrv.enable5vGpio, batteryDrv.enable5vPin, GPIO_PIN_SET);

    HAL_ADCEx_Calibration_Start(adc);
}

uint32_t adcVal[2];
uint32_t sbCount = 0;
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef* hadc) {
    uint32_t currentma = (3300 * adcVal[0]) / (4096);
    // Average it over the last 8 readings
    uint32_t tmp = batteryDrv.currentma * 7 + currentma;
    batteryDrv.currentma = tmp / 8;

    batteryDrv.voltagemv = (2 * 3300 * adcVal[1]) / (4096);
    sbCount++;
}

void loopBattery(uint32_t ticks) {
    static uint32_t nextTicks = 0;
    batteryDrv.charging = (HAL_GPIO_ReadPin(batteryDrv.chargingGpio, batteryDrv.chargingPin) == GPIO_PIN_RESET);
    batteryDrv.standby = (HAL_GPIO_ReadPin(batteryDrv.standbyGpio, batteryDrv.standbyPin) == GPIO_PIN_RESET);

    if (ticks > nextTicks) {
        
        // while (HAL_ADC_GetState(batteryDrv.adc) & HAL_ADC_STATE_BUSY_REG) {}
        // __HAL_ADC_CLEAR_FLAG(batteryDrv.adc, ADC_FLAG_OVR);

        nextTicks = ticks + 40;

        volatile HAL_StatusTypeDef result = HAL_ADC_Start_DMA(batteryDrv.adc, adcVal, 2);
        myAssert(HAL_OK == result, "");

        // myAssert(HAL_OK == HAL_ADC_Start(batteryDrv.adc), "");
        // for (uint32_t i=0; i<2; i++) {
        // 	volatile HAL_StatusTypeDef result = HAL_ADC_PollForConversion(batteryDrv.adc, 20);
        //     myAssert(HAL_OK == result, "");
        //     adcVal[i] = HAL_ADC_GetValue(batteryDrv.adc);
        //     if (result == HAL_OK) {
        //     	if (i == 0) {
        //     		batteryDrv.currentma = (3300 * adcVal[0]) / (4095);
        //     	} else {
        //     		batteryDrv.voltagemv = (2 * 3300 * adcVal[1]) / (4095);
        //     		sbCount++;
        //     	}
        //     }
        // }
        // myAssert(HAL_OK == HAL_ADC_Stop(batteryDrv.adc), "");

        uint32_t charge[] = {
            4200, 100,
            4150, 95,
            4100, 90,
            4000, 80,
            3900, 70,
            3800, 50,
            3700, 30,
            3600, 20,
            3500, 10,
            3400, 5
        };

        batteryDrv.chargePercent = 0;
        for (uint32_t i=0; i<sizeof(charge)/sizeof(charge[0]); i+=2) {
            if (batteryDrv.voltagemv > charge[i]) {
                batteryDrv.chargePercent = charge[i+1];
                break;
            }
        }
    }
}


#endif
