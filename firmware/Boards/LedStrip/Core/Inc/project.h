
#ifndef PROJECT_H
#define PROJECT_H

#include "stm32g030xx.h" // GPIO defs
#include "stm32g0xx_hal.h" // Get tick, delay
#include "stm32g0xx_hal_tim.h"

#define ENABLE_LED_MODULE
#define LED_TIMER_CLK_FREQUENCY 64000000
#define FIXED_NUM_LEDS 0
#define FIXED_LED_PANEL_WIDTH 0

#define MAX_MICROBUS_TX_PACKETS 4
#define MAX_MICROBUS_RX_PACKETS 4

#endif
