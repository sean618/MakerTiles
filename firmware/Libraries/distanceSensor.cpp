
#include "project.h"
#ifdef ENABLE_DISTANCE_MODULE

#include <stdio.h>
#include <math.h>
#include <string.h>
#include <stdint.h>

#include "vl53lx_api.h"

// Implement a median filter
#define MAX_FILTER_VALUES 128
typedef struct {
	int16_t sortedDistance[MAX_FILTER_VALUES];
    uint8_t age[MAX_FILTER_VALUES];
} tRecordedValues;

typedef struct sDistanceSensorDriver {
   uint8_t appMajorVersion;
   uint8_t appMinorVersion;
   I2C_HandleTypeDef *  i2c;
   uint32_t numObjects;
   int16_t distance[4];
   uint8_t status[4];
   tRecordedValues readings;
   uint32_t numFilter;
} DistanceSensorDriver;

static DistanceSensorDriver distDrv = {};

// ================================================ //

const fp::FieldEntry distanceSensorFields[] = {
   { &distDrv.numObjects, "num_objects",       1,  fp::FieldDataType::Uint,  1,  fp::FieldFlags::Gettable | fp::FieldFlags::Joined,  NULL, NULL, NULL},
   { &distDrv.distance,   "distances",         4,  fp::FieldDataType::Int,   2,  fp::FieldFlags::Gettable | fp::FieldFlags::Joined,  NULL, NULL, NULL},
   { &distDrv.numFilter,   "num_filter_samples",  4,  fp::FieldDataType::Int,   1,  fp::FieldFlags::Gettable | fp::FieldFlags::Joined,  NULL, NULL, NULL},
//   { &distDrv.min,        "min",               1,  fp::FieldDataType::Int,   2,  fp::FieldFlags::Gettable | fp::FieldFlags::Joined,  NULL, NULL, NULL},
//   { &distDrv.max,        "max",               1,  fp::FieldDataType::Int,   2,  fp::FieldFlags::Gettable | fp::FieldFlags::Joined,  NULL, NULL, NULL},
//   { &distDrv.sigma,      "sigma",             1,  fp::FieldDataType::Uint,  4,  fp::FieldFlags::Gettable         ,  NULL, NULL, NULL},
};
const fp::FieldTable distanceFieldTable = {
    .fields = (fp::FieldEntry*) distanceSensorFields,
    .numFields = sizeof(distanceSensorFields)/sizeof(fp::FieldEntry)
};

// ======================== //

VL53LX_Dev_t device;

uint32_t timeTaken;
uint32_t sbnumObjects[100] = {0};
uint32_t sbindex =0;

static void addNewDistanceReading(tRecordedValues * state, int16_t distance) {
    for (uint32_t i=0; i<distDrv.numFilter; i++) {
        state->age[i]++;
    }

    // Shift out the previous value
    for (uint32_t i=0; i<distDrv.numFilter; i++) {
        if (state->age[i] >= distDrv.numFilter) {
            for (uint32_t j=i; j<distDrv.numFilter-1; j++) {
                state->sortedDistance[j] = state->sortedDistance[j+1];
                state->age[j]            = state->age[j+1];
            }
            break;
        }
    }
    // Now add in the new value
    for (uint32_t i=0; i<distDrv.numFilter-1; i++) {
        if (distance < state->sortedDistance[i]) {
            // Shift everything up
            for (uint32_t j=distDrv.numFilter-1; j>i; j--) {
                state->sortedDistance[j] = state->sortedDistance[j-1];
                state->age[j]            = state->age[j-1];
            }
            state->sortedDistance[i] = distance;
            state->age[i]            = 0;
            return;
        }
    }
    state->sortedDistance[distDrv.numFilter-1] = distance;
    state->age[distDrv.numFilter-1] = 0;
}


// Each distance reading typically takes 30ms but sometimes it takes 100ms
void updateDistance() {

    uint32_t start = HAL_GetTick();

    while (1) {
        uint8_t NewDataReady=0;
        int status = VL53LX_GetMeasurementDataReady(&device, &NewDataReady);
        if((!status) && (NewDataReady != 0)){
            uint32_t numObjects = 0;
            VL53LX_MultiRangingData_t tmp;
            status = VL53LX_GetMultiRangingData(&device, &tmp);
            if (status == 0){
                // for (uint32_t i=0; i<tmp.NumberOfObjectsFound; i++) {
                //     if (tmp.RangeData[i].RangeStatus == 0) {
                //         distDrv.distance[i] = tmp.RangeData[i].RangeMilliMeter;
                //         numObjects++;
                //     }
                // }

                if (tmp.RangeData[0].RangeStatus == 0) {
//                    distDrv.min = tmp.RangeData[0].RangeMinMilliMeter;
//                    distDrv.max = tmp.RangeData[0].RangeMaxMilliMeter;
//                    distDrv.sigma = tmp.RangeData[0].SigmaMilliMeter/65536;

                    addNewDistanceReading(&distDrv.readings, tmp.RangeData[0].RangeMilliMeter);


                    distDrv.distance[0] = distDrv.readings.sortedDistance[distDrv.numFilter/2];
                    numObjects++;
                }

                status = VL53LX_ClearInterruptAndStartMeasurement(&device);
            }
            if (numObjects > 0) {
                distDrv.numObjects = numObjects;
                break;
            }
        }
    }

    timeTaken = HAL_GetTick() - start;

    sbnumObjects[sbindex] = timeTaken;
    sbindex++;
    if (sbindex >= 100) {
        sbindex = 0;
    }

}



void initDistanceSensor(GPIO_TypeDef* enableGpio, uint16_t enableGpioPin, I2C_HandleTypeDef * i2c) {
    distDrv.numFilter = 1;

    distDrv.i2c = i2c;
    HAL_GPIO_WritePin(enableGpio, enableGpioPin, GPIO_PIN_SET);

    device.I2cHandle = i2c;
    device.I2cDevAddr = 0x52;

    VL53LX_Error status;
    status = VL53LX_WaitDeviceBooted(&device);

    uint8_t revisionMajor, revisionMinor;
    VL53LX_DeviceInfo_t deviceInfo;
    uint64_t uid;
    VL53LX_GetProductRevision(&device, &revisionMajor, &revisionMinor);
    VL53LX_GetDeviceInfo(&device, &deviceInfo);
    VL53LX_GetUID(&device, &uid);

    status = VL53LX_DataInit(&device);

    VL53LX_set_tuning_parm(&device, VL53LX_TUNINGPARM_PHASECAL_PATCH_POWER, 1);

    status = VL53LX_StartMeasurement(&device);

}

void loopDistanceSensor(uint32_t ticks) {
//    static uint32_t nextTicks = 0;
//    if (ticks > nextTicks) {
//        nextTicks = ticks + 10;
       updateDistance();
//    }
}

#endif //ENABLE_DISTANCE_MODULE
