
#ifndef PROJECT_H
#define PROJECT_H

#include "stm32f1xx.h" // GPIO defs
#include "stm32f1xx_hal.h" // Get tick, delay
#include "stm32f1xx_hal_tim.h"

#define USE_SPI_MASTER

#define MAX_USB_TX_PACKETS 10
#define MAX_USB_RX_PACKETS 10

#define MAX_MICROBUS_TX_PACKETS 32
#define MAX_MICROBUS_RX_PACKETS 5

 #define RETURN_CREDITS_BURST_SIZE MAX_MICROBUS_TX_PACKETS

#endif
