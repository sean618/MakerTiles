
#ifndef PROJECT_H
#define PROJECT_H

#include "stm32f1xx.h" // GPIO defs
#include "stm32f1xx_hal.h" // Get tick, delay
#include "stm32f1xx_hal_tim.h"
#include "FieldProtocol/fpCommon.hpp"
#include "Libraries/myAssert.h"

#define ENABLE_DISTANCE_MODULE

#define MAX_MICROBUS_TX_PACKETS 4
#define MAX_MICROBUS_RX_PACKETS 4

#define STM32F1

#endif
