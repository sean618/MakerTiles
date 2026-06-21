/*
 * Copyright © 2024 - 2024 Sean Bremner. All rights reserved.>>
 */

// ============================================================== //
// Buffered USB network configuration.
// ============================================================== //

#include <cstdint>
#include <cstring>

#include "../src/fpCommon.hpp"
#include "testCommon.hpp"
#include "usbMock.hpp"

using namespace fp;

namespace {

constexpr uint8_t kMaxDaemonTxBufferSize = 10;
constexpr uint8_t kMaxMasterDaemonTxBufferSize = 10;  // TODO: decrease
constexpr uint8_t kMaxDaemonRxBufferSize = 10;
constexpr uint8_t kMaxMasterDaemonRxBufferSize = 5;

uint8_t daemonTxBufferData[kMaxDaemonTxBufferSize * (2 + kMaxDaemonPacketSize)] = {0};
uint8_t masterDaemonTxBufferData[kMaxMasterDaemonTxBufferSize * (2 + kMaxDaemonPacketSize)] = {0};
uint8_t daemonRxBufferData[kMaxDaemonRxBufferSize * (2 + kMaxDaemonPacketSize)] = {0};
uint8_t masterDaemonRxBufferData[kMaxMasterDaemonRxBufferSize * (2 + kMaxDaemonPacketSize)] = {0};

TestTxBuffer daemonTxBuffer;
TestTxBuffer masterDaemonTxBuffer;
TestRxBuffer daemonRxBuffer;
TestRxBuffer masterDaemonRxBuffer;

constexpr uint32_t maxRxCredits = 3;

}  // namespace

TestInterface usbMockDaemonItf;
TestInterface usbMockMasterDaemonItf;

uint32_t usbMockGetMaxRxCredits() {
    return maxRxCredits;
}

// Master's USB tx buffer.
uint32_t usbMockGetNumTxInUsbBuffer() {
    TestTxBuffer* buffer = &masterDaemonTxBuffer;
    return circularBufferLength(buffer->start, buffer->end, buffer->size);
}

// =================================== //

void usbMockInitNetwork() {
    initTxBuffer(&daemonTxBuffer, daemonTxBufferData, kMaxDaemonPacketSize, kMaxDaemonTxBufferSize, nullptr);
    initTxBuffer(&masterDaemonTxBuffer, masterDaemonTxBufferData, kMaxDaemonPacketSize,
                 kMaxMasterDaemonTxBufferSize, nullptr);
    initRxBuffer(&daemonRxBuffer, daemonRxBufferData, kMaxDaemonPacketSize, kMaxDaemonRxBufferSize, nullptr);
    initRxBuffer(&masterDaemonRxBuffer, masterDaemonRxBufferData, kMaxDaemonPacketSize,
                 kMaxMasterDaemonRxBufferSize, nullptr);

    usbMockDaemonItf.txBuffer = &daemonTxBuffer;
    usbMockDaemonItf.rxBuffer = &daemonRxBuffer;
    usbMockDaemonItf.isDaemon = true;
    usbMockDaemonItf.daemonMasterLink = true;

    usbMockMasterDaemonItf.txBuffer = &masterDaemonTxBuffer;
    usbMockMasterDaemonItf.rxBuffer = &masterDaemonRxBuffer;
    usbMockMasterDaemonItf.isDaemon = false;
    usbMockMasterDaemonItf.daemonMasterLink = true;
}

void usbMockTransferAllTxToRx() {
    transferTxToRx(false, &masterDaemonTxBuffer, &daemonRxBuffer);
    transferTxToRx(true, &daemonTxBuffer, &masterDaemonRxBuffer);
}

// =================================== //
// For the Python daemon to interact with the system.

uint32_t daemonUsbSendTxPacket(uint8_t* data, uint16_t length) {
    // The Python daemon batches several field-protocol packets into a single USB
    // write (see tx_manager._process_tx). The real transport is a byte stream that
    // the firmware unframes using each packet's leading 2-byte little-endian length
    // (field_protocol.request_to_packet), so do the same here and submit each
    // packet separately - otherwise only the first request in a batch is seen.
    uint16_t offset = 0;
    while (offset < length) {
        uint16_t packetSize = data[offset] | (data[offset + 1] << 8);
        if (packetSize < 2 || offset + packetSize > length) {
            break;  // malformed or partial trailing packet
        }
        uint8_t* packet = testAllocateTxPacket(daemonTxBuffer);
        if (packet == nullptr) {
            break;  // tx buffer full - report what we managed to queue
        }
        std::memcpy(packet, &data[offset], packetSize);
        testSubmitAllocatedTxPacket(daemonTxBuffer, 0, packetSize);
        offset += packetSize;
    }
    return offset;
}

// Return number of bytes.
uint16_t daemonUsbGetRxPacket(uint8_t* data) {
    uint16_t numBytes = 0;
    uint8_t srcNodeId = 0;
    uint8_t* packet = testPeekRxPacket(daemonRxBuffer, numBytes, srcNodeId);
    if (packet == nullptr) {
        return 0;
    }
    testPopRxPacket(daemonRxBuffer);
    std::memcpy(data, packet, numBytes);
    return numBytes;
}

uint32_t daemonNumInUsbTxBuffer() {
    return circularBufferLength(daemonTxBuffer.start, daemonTxBuffer.end, daemonTxBuffer.size);
}
