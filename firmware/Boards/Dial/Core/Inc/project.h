
#ifndef PROJECT_H
#define PROJECT_H

#include "stm32g030xx.h" // GPIO defs
#include "stm32g0xx_hal.h" // Get tick, delay
#include "stm32g0xx_hal_tim.h"
#include "FieldProtocol/fpCommon.hpp"
#include "Libraries/myAssert.h"

#define ENABLE_ROTARY_ENCODER_MODULE
#define ENABLE_BUTTONS_MODULE

#define NUM_BUTTONS 1

#define BUTTON_FIELDS_OFFSET NUM_ROTARY_ENCODER_FIELDS


#define MAX_MICROBUS_TX_PACKETS 4 // Need 4 if we want max data rate from node to master
#define MAX_MICROBUS_RX_PACKETS 4

#endif
