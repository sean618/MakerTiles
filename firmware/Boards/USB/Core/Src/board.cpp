/*
 * Copyright © 2024 - 2024 Sean Bremner. All rights reserved.
 *
 * USB master board, ported to the C++ FieldProtocol + MicroBus libraries.
 *
 * Layering:
 *   - The daemon (host PC) talks to us over USB (CDC). DaemonUsbInterface
 *     adapts that link to fp::FpInterface.
 *   - The microbus (SPI) link to the field nodes is driven by the C++
 *     microbus::Master (see stm32F1SpiMaster). MicrobusInterface adapts it to
 *     fp::FpInterface.
 *   - fp::Master bridges daemon <-> microbus and also hosts this board's own
 *     fields via fp::Node.
 */

#include <cstdint>
#include <cstring>

#include "stm32f1xx_hal.h"
#include "usbd_cdc_if.h"

#include "FieldProtocol/fpCommon.hpp"
#include "FieldProtocol/fpMaster.hpp"
#include "FieldProtocol/fpNode.hpp"
#include "microbus/microbus.hpp"

#include "stm32/stm32F1SpiMaster.hpp"
#include "Libraries/myAssert.h"
#include "Libraries/useful.h"

#include "project.h"

extern SPI_HandleTypeDef hspi1;
extern TIM_HandleTypeDef htim1;
extern TIM_HandleTypeDef htim2;
extern USBD_HandleTypeDef hUsbDeviceFS;

// The daemon packet is the largest thing on the USB link.
static constexpr uint16_t kMaxDaemonPacketSize = fp::kMaxDaemonPacketSize;

// ==================================== //
// Buffer functions - TODO: put somewhere common

struct PacketBuffer {
    uint16_t start;
    uint16_t end;
    uint16_t size;
    uint8_t* data;
    uint32_t maxLen;
};

static uint8_t usbTxBufferPackets[MAX_USB_TX_PACKETS * kMaxDaemonPacketSize] = {0};
static uint8_t usbRxBufferPackets[MAX_USB_RX_PACKETS * kMaxDaemonPacketSize] = {0};
static uint16_t usbTxPacketSizes[MAX_USB_TX_PACKETS] = {0};
static uint16_t usbRxPacketSizes[MAX_USB_RX_PACKETS] = {0};

static PacketBuffer usbTxBuffer = {0, 0, MAX_USB_TX_PACKETS, usbTxBufferPackets, 0};
static PacketBuffer usbRxBuffer = {0, 0, MAX_USB_RX_PACKETS, usbRxBufferPackets, 0};

static uint64_t usbRxFullCount = 0;
static uint32_t sbmaxUsbRxLevel = 0;
uint64_t numUsbRxPackets = 0;
uint64_t numUsbFpRxPackets = 0;

static bool usbTxAllocated = false;

static uint8_t* usbTxBufferAllocateTxPacket() {
    myAssert(usbTxAllocated == false, "");
    if (fp::circularBufferFull(usbTxBuffer.start, usbTxBuffer.end, usbTxBuffer.size)) {
        return nullptr;
    }
    usbTxAllocated = true;
    return &usbTxBuffer.data[kMaxDaemonPacketSize * usbTxBuffer.end];
}

static void usbTxSubmitAllocatedTxPacket(uint16_t packetSize) {
    usbTxPacketSizes[usbTxBuffer.end] = packetSize;
    usbTxBuffer.end = fp::incrAndWrap(usbTxBuffer.end, 1, usbTxBuffer.size);
    usbTxAllocated = false;
}

static uint8_t* usbPeekRxPacket(uint16_t& numBytes, uint8_t& srcNodeId) {
    if (fp::circularBufferEmpty(usbRxBuffer.start, usbRxBuffer.end, usbRxBuffer.size)) {
        return nullptr;
    }
    numBytes = usbRxPacketSizes[usbRxBuffer.start];
    srcNodeId = 0;
    return &usbRxBuffer.data[kMaxDaemonPacketSize * usbRxBuffer.start];
}

static bool usbPopRxPacket() {
    myAssert(!fp::circularBufferEmpty(usbRxBuffer.start, usbRxBuffer.end, usbRxBuffer.size), "");
    usbRxBuffer.start = fp::incrAndWrap(usbRxBuffer.start, 1, usbRxBuffer.size);
    return true;
}

// ==================================== //
// USB rx reassembly

static bool usbTransmissionInProgress() {
    USBD_CDC_HandleTypeDef* hcdc = (USBD_CDC_HandleTypeDef*)hUsbDeviceFS.pClassData;
    return hcdc->TxState != 0;
}

static uint32_t usbRxCurrentPacketBytesProcessed = 0;  // Length processed so far
static uint32_t usbRxCurrentPacketSize = 0;
uint8_t callDepth = 0;

// USB packets are 64 bytes - so we need to reassemble them into larger FP packets.
// Also it seems like the FP packets are sometimes combined together in a single USB
// packet so we need to separate them.
extern "C" void usbRxCallback(uint8_t* Buf, uint32_t* Len) {
    uint32_t tmplen = fp::circularBufferLength(usbRxBuffer.start, usbRxBuffer.end, usbRxBuffer.size);
    if (tmplen > sbmaxUsbRxLevel) {
        sbmaxUsbRxLevel = tmplen;
    }
    numUsbRxPackets++;
    // Check if buffer is full
    if (fp::circularBufferFull(usbRxBuffer.start, usbRxBuffer.end, usbRxBuffer.size)) {
        myAssert(0, "");
        usbRxFullCount++;
        return;
    }

    uint32_t numBytesRemaining = *Len;

    while (numBytesRemaining) {
        uint8_t* remainingBytes = &Buf[*Len - numBytesRemaining];

        // New FP packet
        if (usbRxCurrentPacketSize == 0) {
            usbRxCurrentPacketBytesProcessed = 0;
            fp::RawDaemonFieldPacket* packet = (fp::RawDaemonFieldPacket*)remainingBytes;
            // Discard the packet if the header size is bigger than the max.
            if (packet->packetSize > kMaxDaemonPacketSize || packet->packetSize == 0) {
                myAssert(0, "");
                return;
            }
            usbRxCurrentPacketSize = packet->packetSize;
            usbRxPacketSizes[usbRxBuffer.end] = usbRxCurrentPacketSize;
        }

        // If the remaining bytes fit into the current packet
        if (usbRxCurrentPacketBytesProcessed + numBytesRemaining <= usbRxCurrentPacketSize) {
            memcpy(&usbRxBuffer.data[usbRxBuffer.end * kMaxDaemonPacketSize + usbRxCurrentPacketBytesProcessed],
                   remainingBytes, numBytesRemaining);
            usbRxCurrentPacketBytesProcessed += numBytesRemaining;
            numBytesRemaining = 0;
            // If finished packet
            if (usbRxCurrentPacketBytesProcessed == usbRxCurrentPacketSize) {
                usbRxBuffer.end = fp::incrAndWrap(usbRxBuffer.end, 1, usbRxBuffer.size);
                usbRxBuffer.maxLen = MAX(usbRxBuffer.maxLen,
                                         fp::circularBufferLength(usbRxBuffer.start, usbRxBuffer.end, usbRxBuffer.size));
                usbRxCurrentPacketSize = 0;
                numUsbFpRxPackets++;
            }
        } else {
            // If remaining bytes overflow the packet then fill the current packet and prepare the next.
            uint32_t numRemainingPacketBytes = usbRxCurrentPacketSize - usbRxCurrentPacketBytesProcessed;
            memcpy(&usbRxBuffer.data[usbRxBuffer.end * kMaxDaemonPacketSize + usbRxCurrentPacketBytesProcessed],
                   remainingBytes, numRemainingPacketBytes);
            usbRxBuffer.end = fp::incrAndWrap(usbRxBuffer.end, 1, usbRxBuffer.size);
            usbRxBuffer.maxLen = MAX(usbRxBuffer.maxLen,
                                     fp::circularBufferLength(usbRxBuffer.start, usbRxBuffer.end, usbRxBuffer.size));
            usbRxCurrentPacketSize = 0;
            numBytesRemaining -= numRemainingPacketBytes;
            numUsbFpRxPackets++;
        }
    }
}

static void processUsbTx() {
    if (fp::circularBufferEmpty(usbTxBuffer.start, usbTxBuffer.end, usbTxBuffer.size)) {
        return;
    }
    if (usbTransmissionInProgress()) {
        return;
    }
    myAssert(CDC_Transmit_FS(&usbTxBuffer.data[usbTxBuffer.start * kMaxDaemonPacketSize],
                             usbTxPacketSizes[usbTxBuffer.start]) == USBD_OK, "");
    usbTxBuffer.start = fp::incrAndWrap(usbTxBuffer.start, 1, usbTxBuffer.size);
}

static uint32_t getNumTxInUsbBuffer() {
    return fp::circularBufferLength(usbTxBuffer.start, usbTxBuffer.end, usbTxBuffer.size);
}

// ==================================== //
// Daemon (USB) interface

class DaemonUsbInterface : public fp::FpInterface {
public:
    DaemonUsbInterface() {
        isDaemon = false;
        daemonMasterLink = true;
    }
    uint8_t* allocateTxPacket() override { return usbTxBufferAllocateTxPacket(); }
    void submitTxPacket(uint8_t /*dstNodeId*/, uint16_t packetSize) override {
        usbTxSubmitAllocatedTxPacket(packetSize);
    }
    uint8_t* peekRxPacket(uint16_t& numBytes, uint8_t& srcNodeId) override {
        return usbPeekRxPacket(numBytes, srcNodeId);
    }
    bool popRxPacket() override { return usbPopRxPacket(); }
};

// ==================================== //
// Microbus (SPI) interface

static HwMicrobusMaster master;

class MicrobusInterface : public fp::FpInterface {
public:
    uint8_t* allocateTxPacket() override { return master.allocateTxPacket(); }
    void submitTxPacket(uint8_t dstNodeId, uint16_t packetSize) override {
        master.submitTxPacket(dstNodeId, packetSize);
    }
    uint8_t* peekRxPacket(uint16_t& numBytes, uint8_t& srcNodeId) override {
        return master.peekRxData(&numBytes, &srcNodeId);
    }
    bool popRxPacket() override { return master.popRxData(); }
    uint8_t getNodeId() const override { return fp::kMasterNodeId; }
};

static DaemonUsbInterface daemonItf;
static MicrobusInterface microbusItf;
static fp::Master fpMaster;

static void getConnectedNodesBitField(uint8_t (&bitfield)[fp::kMaxNumNodes / 8]) {
    master.getConnectedNodes(bitfield);
}

// ==================================== //
// Board fields

static bool sendContinuously = false;
static uint32_t testBuffer[45 + 8] = {0};

// credits_in_use is computed on demand.
static uint8_t* getCreditsInUse(fp::FieldEntry* /*field*/, fp::FieldIndex /*fieldOffset*/,
                                fp::FieldIndex /*numFields*/) {
    static uint32_t tmpCreditsInUse;
    tmpCreditsInUse = fp::circularBufferLength(usbRxBuffer.start, usbRxBuffer.end, usbRxBuffer.size)
                      + master.txBufferedFor(fp::kMasterNodeId);
    return reinterpret_cast<uint8_t*>(&tmpCreditsInUse);
}

fp::FieldEntry usbFields[] = {
    // ptr            ,                    name             span   type                       size    flags                                              setFn  getFn  units
    {  &usbRxFullCount,                    "usb_rx_full",       1, fp::FieldDataType::Uint,      8, fp::FieldFlags::Gettable | fp::FieldFlags::Settable, nullptr, nullptr, ""},
    {  &usbRxBuffer.maxLen,                "max_rx_level",      1, fp::FieldDataType::Uint,      4, fp::FieldFlags::Gettable | fp::FieldFlags::Settable, nullptr, nullptr, ""},
    {  &spiMaster.numSpiResets,            "numSpiResets",      1, fp::FieldDataType::Uint,      4, fp::FieldFlags::Gettable | fp::FieldFlags::Settable, nullptr, nullptr, ""},
    {  &testBuffer,                        "test_buffer",      45, fp::FieldDataType::Uint,      4, fp::FieldFlags::Gettable | fp::FieldFlags::Settable, nullptr, nullptr, ""},
    {  nullptr,                            "credits_in_use",    1, fp::FieldDataType::Uint,      4, fp::FieldFlags::Gettable,                            nullptr, getCreditsInUse, ""},
    {  &sendContinuously,                  "sendContinuously",  1, fp::FieldDataType::Boolean,   1, fp::FieldFlags::Gettable | fp::FieldFlags::Settable, nullptr, nullptr, ""},
};
fp::FieldTable usbFieldTable = {
    usbFields,
    sizeof(usbFields) / sizeof(fp::FieldEntry),
};

const fp::FieldTable* fieldTables[] = {
    &usbFieldTable,
    &assertFieldTable,
};

static fp::BoardInfo boardInfo = {
    "usb",  // boardName
    "usb",  // customName
    0,      // uniqueId
    0,      // numFields (filled in by init)
};

// ==================================== //

static uint64_t readUniqueID() {
    uint32_t tmp[3] = {HAL_GetUIDw0(), HAL_GetUIDw1(), HAL_GetUIDw2()};
    uint64_t res = tmp[0];
    res = (res << 32) | tmp[1];
    return res;
}

// Called every 0.2ms
static uint64_t txCreditsSent = 0;
extern "C" void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef* htim) {
    if (htim == &htim2) {
        if (sendContinuously) {
            fpMaster.sendFields(2, 1);
        }

        master.updateTimeUs(200);
        // Can't write to totalTxPacketsFreed as it is accessed by a higher priority thread.
        uint64_t numTxPacketsFreed = totalTxPacketsFreed - txCreditsSent;
        myAssert(numTxPacketsFreed < 0xFFFFFFFF, "");
        fpMaster.processAllRx(static_cast<uint8_t>(numTxPacketsFreed));
        txCreditsSent += numTxPacketsFreed;
        processUsbTx();
        timerCallback();
    }
}

extern "C" void boardInit() {
    boardInfo.uniqueId = readUniqueID();

    // MicroBus master: schedule kMaxScheduledSlots tx nodes per cycle.
    master.init(microbus::kMaxScheduledSlots);
    hwMasterInit(&master, &hspi1, 1, GPIOA, GPIO_PIN_8, &htim1);

    // Field Protocol master. It hosts this board's own fields (fieldTables) and
    // bridges the daemon (USB) and microbus (SPI) links.
    fpMaster.init(daemonItf, microbusItf, boardInfo, fieldTables,
                  sizeof(fieldTables) / sizeof(fp::FieldTable*),
                  getConnectedNodesBitField, getNumTxInUsbBuffer,
                  MAX_USB_RX_PACKETS, MAX_MICROBUS_TX_PACKETS);

    // TODO: should we be doing this here?
    myAssert(HAL_TIM_Base_Start(&htim1) == HAL_OK, "");
    myAssert(HAL_TIM_Base_Start_IT(&htim2) == HAL_OK, "");
    hwMasterStart();
}

extern "C" void boardLoop() {
}
