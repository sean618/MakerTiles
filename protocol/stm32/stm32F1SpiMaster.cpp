/*
 * Copyright © 2024 - 2024 Sean Bremner. All rights reserved.
 *
 * C++ port of stm32F1SpiMaster.c, driving the new C++ microbus::Master.
 */

#include "project.h"

#ifdef USE_SPI_MASTER

#include <cstdint>
#include <cstring>

#include "stm32f1xx_hal.h"
#include "microbus/src/microbus.hpp"
#include "stm32F1SpiMaster.hpp"
#include "Libraries/myAssert.h"

// ================================ //
//
// Master Requirements:
//  - SPI master configured:
//      - Tx DMA normal
//      - Rx DMA normal
//  - GPIO output for the PS signal
//  - A timer set to count every 1/10th of a tick
//
// ================================ //

SpiMaster spiMaster;

extern "C" {
volatile uint32_t totalTxPacketsFreed = 0;
}

namespace {

constexpr uint32_t kResetSpiCount = 50;

// The DMA transfers (MB_PACKET_SIZE-1)/2 16-bit words, matching the original.
constexpr uint16_t kDmaWordCount =
    static_cast<uint16_t>((microbus::kPacketSize - 1) / 2);

inline void delayUs(TIM_HandleTypeDef* usTimer, uint32_t usec) {
    // A 16 bit timer counting in 1/10th of a microsec wraps every ~65 ms.
    uint16_t timerStart = __HAL_TIM_GET_COUNTER(usTimer);
    uint16_t timerEnd = static_cast<uint16_t>(timerStart + usec);
    bool wrapped = timerStart > timerEnd;
    while (true) {
        volatile uint32_t time = __HAL_TIM_GET_COUNTER(usTimer);
        if (time >= timerEnd && (!wrapped || (time < timerStart))) {
            break;
        }
    }
}

}  // namespace

extern "C" void HAL_SPI_ErrorCallback(SPI_HandleTypeDef* hspi) {
    HAL_SPI_TxRxCpltCallback(hspi);
}

extern "C" void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef* hspi) {
    myAssert(hspi == spiMaster.hspi, "");
    bool crcError = (HAL_SPI_GetError(hspi) == HAL_SPI_ERROR_CRC);
    spiMaster.numSpiCallbacks++;

    // Track whether we are hearing nodes, so we can reset a wedged SPI.
    // numActiveNodes() is the side-effect-free equivalent of the original
    // master->activeNodes.numNodes read.
    if (spiMaster.master->numActiveNodes() > 0) {
        spiMaster.countSinceHeardNode = 0;
    } else {
        spiMaster.countSinceHeardNode++;
    }

    if (spiMaster.countSinceHeardNode > kResetSpiCount) {
        spiMaster.countSinceHeardNode = 0;
        // TODO: only valid for SPI1
        if (spiMaster.spiIndex == 1) {
            __HAL_RCC_SPI1_FORCE_RESET();
            __HAL_RCC_SPI1_RELEASE_RESET();
        } else {
            myAssert(spiMaster.spiIndex == 2, "");
            __HAL_RCC_SPI2_FORCE_RESET();
            __HAL_RCC_SPI2_RELEASE_RESET();
        }
        myAssert(HAL_OK == HAL_SPI_Init(spiMaster.hspi), "");
    }

    // Set pin to tell nodes a transaction is about to start
    HAL_GPIO_WritePin(spiMaster.psGpioGroup, spiMaster.psGpioPin, GPIO_PIN_SET);

    // Get our next data ptrs to use. preProcess in the old API both fetched the
    // buffers and processed the previous rx; fullDuplexBeginSlot only fetches
    // them (the rx processing now happens in fullDuplexEndSlot below).
    microbus::Packet* masterTxPacket = nullptr;
    microbus::Packet* masterRxPacket = nullptr;
    spiMaster.master->fullDuplexBeginSlot(&masterTxPacket, &masterRxPacket, crcError);

    // Wait to give nodes a chance to start their TxRx DMAs
    delayUs(spiMaster.usTimer, 40);  // TODO- check

    // TODO: look into
    if (hspi->hdmatx->State != HAL_DMA_STATE_READY) {
        delayUs(spiMaster.usTimer, 35);
        HAL_GPIO_WritePin(spiMaster.psGpioGroup, spiMaster.psGpioPin, GPIO_PIN_RESET);
        goto EXIT;
    }

    // Start the transaction.
    __HAL_SPI_CLEAR_CRCERRFLAG(hspi);
    {
        volatile int res = HAL_SPI_TransmitReceive_DMA(
            hspi, reinterpret_cast<uint8_t*>(masterTxPacket),
            reinterpret_cast<uint8_t*>(masterRxPacket), kDmaWordCount);
        myAssert(res == HAL_OK, "SPI TxRx failed");
    }

    // Reset PS line ready for next transaction.
    HAL_GPIO_WritePin(spiMaster.psGpioGroup, spiMaster.psGpioPin, GPIO_PIN_RESET);

EXIT:
    // Now process the packet we just received and prepare the next tx.
    uint8_t numTxPacketsFreed = spiMaster.master->fullDuplexEndSlot();
    totalTxPacketsFreed += numTxPacketsFreed;
}

void hwMasterInit(HwMicrobusMaster* master,
                  SPI_HandleTypeDef* hspi,
                  uint32_t spiIndex,
                  GPIO_TypeDef* psGpioGroup,
                  uint16_t psGpioPin,
                  TIM_HandleTypeDef* usTimer) {
    spiMaster = SpiMaster{};
    spiMaster.master = master;
    spiMaster.hspi = hspi;
    spiMaster.spiIndex = spiIndex;
    spiMaster.psGpioGroup = psGpioGroup;
    spiMaster.psGpioPin = psGpioPin;
    spiMaster.usTimer = usTimer;
}

static volatile uint16_t tmpBytes[3];
static uint64_t stateNotReadyCount = 0;

void timerCallback() {
    if (spiMaster.lastNumSpiCallbacks == spiMaster.numSpiCallbacks) {
        spiMaster.resetSpiCount++;
    } else {
        spiMaster.resetSpiCount = 0;
        spiMaster.lastNumSpiCallbacks = spiMaster.numSpiCallbacks;
    }

    if (spiMaster.resetSpiCount > 10) {
        spiMaster.resetSpiCount = 0;
        spiMaster.numSpiResets++;
        if (spiMaster.hspi->hdmatx->State == HAL_DMA_STATE_READY) {
            stateNotReadyCount = 0;
            volatile int res = HAL_SPI_TransmitReceive_DMA(
                spiMaster.hspi, reinterpret_cast<uint8_t*>(const_cast<uint16_t*>(&tmpBytes[0])),
                reinterpret_cast<uint8_t*>(const_cast<uint16_t*>(&tmpBytes[1])), 1);
            myAssert(res == HAL_OK, "SPI restart failed");
        } else {
            stateNotReadyCount++;
        }
    }
}

void hwMasterStart() {
    volatile int res = HAL_SPI_TransmitReceive_DMA(
        spiMaster.hspi, reinterpret_cast<uint8_t*>(const_cast<uint16_t*>(&tmpBytes[0])),
        reinterpret_cast<uint8_t*>(const_cast<uint16_t*>(&tmpBytes[1])), 1);
    myAssert(res == HAL_OK, "");
}

#endif  // USE_SPI_MASTER
