/*
 * Copyright © 2024 - 2024 Sean Bremner. All rights reserved.
 *
 * C++ SPI hardware driver for the MicroBus master.
 *
 * This is the port of the old C stm32F1SpiMaster which drove the old C tMaster.
 * It now drives the new C++ microbus::Master via its full-duplex slot API
 * (fullDuplexBeginSlot / fullDuplexEndSlot).
 *
 * Hardware requirements (unchanged from the original):
 *   - SPI master configured with Tx DMA (normal) and Rx DMA (normal)
 *   - A GPIO output for the PS (packet-start) signal
 *   - A timer counting every 1/10th of a microsecond
 */

#pragma once

#include "project.h"

#ifdef USE_SPI_MASTER

#include <cstdint>

#include "stm32f1xx_hal.h"
#include "microbus/src/microbus.hpp"

// The master's rx queue depth. Kept here so the concrete Master<> type is in
// one place; board code that needs the type uses HwMaster::MasterType.
inline constexpr int kHwMasterRxPacketEntries = MAX_MICROBUS_RX_PACKETS;

extern "C" {

// Total tx packets the bus has freed (acked) since boot. Read by the board's
// field-protocol credit accounting. Written only from the SPI ISR context.
extern volatile uint32_t totalTxPacketsFreed;

}  // extern "C"

using HwMicrobusMaster = microbus::Master<kHwMasterRxPacketEntries>;

// Diagnostic counters mirror the original tSpiMaster struct.
struct SpiMaster {
    HwMicrobusMaster* master = nullptr;
    SPI_HandleTypeDef* hspi = nullptr;
    uint32_t spiIndex = 0;
    GPIO_TypeDef* psGpioGroup = nullptr;
    uint16_t psGpioPin = 0;
    TIM_HandleTypeDef* usTimer = nullptr;  // counts per 1/10th of a us
    uint32_t countSinceHeardNode = 0;
    uint32_t lastNumSpiCallbacks = 0;
    uint32_t numSpiCallbacks = 0;
    uint32_t resetSpiCount = 0;
    uint32_t numSpiResets = 0;
};

extern SpiMaster spiMaster;

// Bind the driver to a configured Master and the SPI/GPIO/timer peripherals.
void hwMasterInit(HwMicrobusMaster* master,
                  SPI_HandleTypeDef* hspi,
                  uint32_t spiIndex,
                  GPIO_TypeDef* psGpioGroup,
                  uint16_t psGpioPin,
                  TIM_HandleTypeDef* usTimer);

// Kick off the first DMA transaction. The SPI complete ISR drives every
// subsequent slot.
void hwMasterStart();

// Called from the periodic timer to detect a stalled SPI/DMA and restart it.
void timerCallback();

#endif  // USE_SPI_MASTER
