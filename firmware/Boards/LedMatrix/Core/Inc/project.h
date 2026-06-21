
#ifndef PROJECT_H
#define PROJECT_H

#include "stm32g030xx.h" // GPIO defs
#include "stm32g0xx_hal.h" // Get tick, delay
#include "stm32g0xx_hal_tim.h"
#include "FieldProtocol/fpCommon.hpp"
#include "Libraries/myAssert.h"

#define ENABLE_LED_MODULE
#define LED_STRIP_MAX_LEDS 49
#define FIXED_NUM_LEDS 49
#define FIXED_LED_PANEL_WIDTH 0

#define MAX_MICROBUS_TX_PACKETS 4 // Need 4 if we want max data rate from node to master
#define MAX_MICROBUS_RX_PACKETS 4

#endif
