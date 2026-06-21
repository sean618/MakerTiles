/*
 * Copyright © 2024 - 2024 Sean Bremner. All rights reserved.
 *
 * Shared node-side protocol driver, ported to the C++ FieldProtocol + MicroBus
 * libraries. This is the node equivalent of the USB master's board.cpp +
 * stm32F1SpiMaster.cpp: it wires an fp::Node to a microbus::Node<> over a
 * SPI-slave link driven by the PS-line EXTI interrupt.
 *
 * Used by all node boards (LedStrip, Battery, Button, ...). The board only
 * declares its field tables and calls protocolInit()/the loop.
 */

#include "project.h"

#ifndef USE_SPI_MASTER

#include <cstdint>
#include <cstring>

#include "microbus/src/microbus.hpp"
#include "FieldProtocol/fpCommon.hpp"
#include "FieldProtocol/fpNode.hpp"
#include "Libraries/myAssert.h"

#define RESET_SPI_COUNT 50

// The node's microbus rx queue depth (must be > 2) and tx window depth. Match
// the board's configured packet counts (the original C node used these), rather
// than the library's default tx depth of 10, to keep RAM within budget.
using NodeType = microbus::Node<MAX_MICROBUS_RX_PACKETS, MAX_MICROBUS_TX_PACKETS>;

static NodeType node;
static fp::Node fpNode;

// Bridges fp::Node <-> the microbus node transport.
class MicrobusInterface : public fp::FpInterface {
public:
    uint8_t* allocateTxPacket() override { return node.allocateTxPacket(); }
    void submitTxPacket(uint8_t dstNodeId, uint16_t packetSize) override {
        node.submitTxPacket(dstNodeId, packetSize);
    }
    uint8_t* peekRxPacket(uint16_t& numBytes, uint8_t& srcNodeId) override {
        return node.peekRxData(&numBytes, &srcNodeId);
    }
    bool popRxPacket() override { return node.popRxData(); }
    uint8_t getNodeId() const override { return node.nodeId; }
};
static MicrobusInterface microbusItf;

// =======================================//

struct SpiNode {
    bool spiStarted = false;
    SPI_HandleTypeDef* hspi = nullptr;
    uint32_t spiIndex = 0;
    GPIO_TypeDef* misoGpioX = nullptr;
    uint16_t misoGpioPin = 0;
    TIM_HandleTypeDef* htim = nullptr;
    uint16_t psGpioPin = 0;
    uint64_t numSpiResets = 0;
    uint64_t goodCycles = 0;
    uint64_t totalCycles = 0;
    uint32_t waitCycles = 0;
    uint32_t savedSpiGpioAlternate = 0;
};

static SpiNode spiNode;

static uint8_t getGPIOAlternateFunction(GPIO_TypeDef* GPIOx, uint16_t gpioPin) {
    uint32_t reg;
    uint8_t bitShift;
    uint8_t pinIndex = 0;

    while ((gpioPin >> pinIndex) != 1U) {
        pinIndex++;
    }

    #ifdef STM32F1
        if (pinIndex < 8) {
            reg = GPIOx->CRL;
            bitShift = pinIndex * 4U;
        } else {
            reg = GPIOx->CRH;
            bitShift = (pinIndex - 8U) * 4U;
        }
    #else
        // Determine which AFR register (AFR[0] or AFR[1]) based on pinIndex
        reg = GPIOx->AFR[(pinIndex / 8U)]; // 0 for pins 0..7, 1 for pins 8..15
        bitShift    = (pinIndex % 8U) * 4U; // Each pin AF uses 4 bits
    #endif

    // Extract the 4-bit mode/config value
    uint8_t af = (reg >> bitShift) & 0x0F;
    return af;
}

#ifdef STM32F1
static void restoreGPIOConfigSTM32F103(GPIO_TypeDef* GPIOx, uint16_t gpioPin, uint8_t savedConfig) {
    uint8_t pinIndex = 0;
    while ((gpioPin >> pinIndex) != 1U) {
        pinIndex++;
    }

    uint32_t* crReg;
    uint8_t bitShift;

    if (pinIndex < 8) {
        crReg = &GPIOx->CRL;
        bitShift = pinIndex * 4U;
    } else {
        crReg = &GPIOx->CRH;
        bitShift = (pinIndex - 8U) * 4U;
    }

    // Clear the old config bits
    *crReg &= ~(0x0F << bitShift);
    // Write the saved config back
    *crReg |= (savedConfig << bitShift);
}
#endif

static inline void setMisoPinMode(bool highImpedance) {
    GPIO_InitTypeDef gpioInit = {0};
    gpioInit.Pin = spiNode.misoGpioPin;
    gpioInit.Pull = GPIO_NOPULL;
    gpioInit.Speed = GPIO_SPEED_FREQ_HIGH;

    #ifndef STM32F1
        gpioInit.Alternate = spiNode.savedSpiGpioAlternate;
    #endif

    if (highImpedance) {
        gpioInit.Mode = GPIO_MODE_INPUT;
    } else {
        gpioInit.Mode = GPIO_MODE_AF_PP;
    }

    HAL_GPIO_Init(spiNode.misoGpioX, &gpioInit);

    #ifdef STM32F1
        restoreGPIOConfigSTM32F103(spiNode.misoGpioX, spiNode.misoGpioPin, spiNode.savedSpiGpioAlternate);
    #endif
}

extern "C" void protocol_GPIO_EXTI_Rising_Callback() {
    if (spiNode.spiStarted == false) {
        return;
    }

    spiNode.totalCycles++;

    if (spiNode.waitCycles > 0) {
        spiNode.waitCycles--;
        return;
    }

    uint32_t error = HAL_SPI_GetError(spiNode.hspi);
    spiNode.hspi->ErrorCode = 0;
    bool crcError = (error == HAL_SPI_ERROR_CRC);

    // If last transaction didn't complete
    if ((spiNode.hspi->State != HAL_SPI_STATE_READY) ||
        (spiNode.hspi->hdmatx->State != HAL_DMA_STATE_READY) ||
        (spiNode.hspi->hdmarx->State != HAL_DMA_STATE_READY) ||
        ((error != HAL_SPI_ERROR_NONE) && !crcError)) {
        // Abort transaction
        volatile HAL_StatusTypeDef res;
        res = HAL_SPI_Abort(spiNode.hspi);
        (void)res;

        // Reset the SPI state
        if (spiNode.spiIndex == 1) {
            __HAL_RCC_SPI1_FORCE_RESET();
            __HAL_RCC_SPI1_RELEASE_RESET();
        } else {
            myAssert(spiNode.spiIndex == 2, "");
            __HAL_RCC_SPI2_FORCE_RESET();
            __HAL_RCC_SPI2_RELEASE_RESET();
        }
        res = HAL_SPI_Init(spiNode.hspi);
        myAssert(res == HAL_OK, "SPI restart failed");

        // Set Tx pin to high impedance so the other nodes can transmit
        setMisoPinMode(true);
        // Record failure
        spiNode.numSpiResets++;
        // Skip the next transaction to give the abort enough time
        spiNode.waitCycles = 10;

    } else {
        // The CRC has to be manually cleared after each transaction
        __HAL_SPI_CLEAR_CRCERRFLAG(spiNode.hspi);

        // Get our next data ptrs to use. fullDuplexBeginSlot fills nodeTxPacket
        // only if it's our turn to transmit (otherwise it stays null); it always
        // fills the rx buffer to receive into.
        microbus::Packet* nodeTxPacket = nullptr;
        microbus::Packet* nextNodeRxPacketMemory = nullptr;
        node.fullDuplexBeginSlot(&nodeTxPacket, &nextNodeRxPacketMemory, crcError);

        // Start next transaction
        if (nodeTxPacket) {
            setMisoPinMode(false);
            volatile int res = HAL_SPI_TransmitReceive_DMA(
                spiNode.hspi, reinterpret_cast<uint8_t*>(nodeTxPacket),
                reinterpret_cast<uint8_t*>(nextNodeRxPacketMemory),
                (microbus::kPacketSize - 1) / 2);
            myAssert(res == HAL_OK, "SPI next TxRx failed");
        } else {
            // Not our turn to transmit so only receive.
            // Set Tx pin to high impedance so the other nodes can transmit.
            setMisoPinMode(true);
            volatile int res = HAL_SPI_Receive_DMA(
                spiNode.hspi, reinterpret_cast<uint8_t*>(nextNodeRxPacketMemory),
                (microbus::kPacketSize - 1) / 2);
            myAssert(res == HAL_OK, "SPI next Rx failed");
        }

        spiNode.goodCycles++;

        // Now process the packet.
        node.fullDuplexEndSlot();
    }
}

extern "C" void protocol_TIM_PeriodElapsedCallback() {
    fpNode.processRxTx();
    node.updateTimeUs(200);
}

// =======================================//

static uint64_t readUniqueID() {
    uint32_t tmp[3] = {HAL_GetUIDw0(), HAL_GetUIDw1(), HAL_GetUIDw2()};
    uint64_t res = tmp[0];
    res = (res << 32) | tmp[1];
    return res;
}

void protocolInit(
        const fp::FieldTable* fieldTables[],
        uint8_t numTables,
        fp::BoardInfo* boardInfo,
        SPI_HandleTypeDef* hspi,
        uint32_t spiIndex,
        GPIO_TypeDef* misoGpioX,
        uint16_t misoGpioPin,
        TIM_HandleTypeDef* htim,
        uint16_t psGpioPin
    ) {
    spiNode = SpiNode{};
    spiNode.hspi = hspi;
    spiNode.spiIndex = spiIndex;
    spiNode.misoGpioX = misoGpioX;
    spiNode.misoGpioPin = misoGpioPin;
    spiNode.htim = htim;
    spiNode.psGpioPin = psGpioPin;
    // When we start, this interrupt might already have triggered and we only
    // want to trigger at exactly the start of the frame, so wait for at least
    // the next interrupt - then the data should be good.
    spiNode.waitCycles = 10;

    spiNode.savedSpiGpioAlternate = getGPIOAlternateFunction(misoGpioX, misoGpioPin);

    boardInfo->uniqueId = readUniqueID();
    node.init(boardInfo->uniqueId);

    fpNode.init(microbusItf, *boardInfo, fieldTables, numTables);
    volatile HAL_StatusTypeDef result = HAL_TIM_Base_Start_IT(htim);
    myAssert(result == HAL_OK, "Starting timer failed");
    spiNode.spiStarted = true;
    setMisoPinMode(true);
}

// Proactively publish a range of fields to the master (streaming/continuous
// sends from the application). Replaces the old C fpNodeSendFields(&fpNode,...)
// — the fp::Node is private to this translation unit, so drivers go through here.
void protocolSendFields(fp::FieldIndex startField, fp::FieldIndex numFields) {
    fpNode.sendFields(startField, numFields);
}

// =======================================//

#ifdef STM32F0
    extern "C" __weak void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
        if (GPIO_Pin == spiNode.psGpioPin) {
            protocol_GPIO_EXTI_Rising_Callback();
        }
    }
#else
    extern "C" __weak void HAL_GPIO_EXTI_Rising_Callback(uint16_t GPIO_Pin) {
        if (GPIO_Pin == spiNode.psGpioPin) {
            protocol_GPIO_EXTI_Rising_Callback();
        }
    }
#endif

extern "C" __weak void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef* htim) {
    if (htim == spiNode.htim) {
        protocol_TIM_PeriodElapsedCallback();
    }
}

#endif  // USE_SPI_MASTER
