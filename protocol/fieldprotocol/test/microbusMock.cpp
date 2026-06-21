/*
 * Copyright © 2024 - 2024 Sean Bremner. All rights reserved.>>
 */

// ============================================================== //
// Buffered network configuration
//
// Unlike the direct-call network configuration this is a straightforward
// buffered network that behaves more like the real thing.
// ============================================================== //

#include <cstdint>
#include <cstring>

#include "../src/fpCommon.hpp"
#include "microbusMock.hpp"
#include "testCommon.hpp"

using namespace fp;

namespace {

constexpr uint8_t kMaxMasterNodeTxBufferSize = 5;
constexpr uint8_t kMaxNodeTxBufferSize = 3;
constexpr uint8_t kMaxMasterNodeRxBufferSize = 5;
constexpr uint8_t kMaxNodeRxBufferSize = 5;

constexpr uint8_t kNumNodes = kMaxNumNodes;

uint8_t masterNodeRxSrcNodeIds[kMaxMasterNodeRxBufferSize];
uint8_t masterNodeTxDstNodeIds[kMaxMasterNodeTxBufferSize];

uint8_t masterNodeTxBufferData[kMaxMasterNodeTxBufferSize * (2 + kMaxBusPacketSize)] = {0};
uint8_t masterNodeRxBufferData[kMaxMasterNodeRxBufferSize * (2 + kMaxBusPacketSize)] = {0};
uint8_t nodeTxBufferData[kNumNodes][kMaxNodeTxBufferSize * (2 + kMaxBusPacketSize)] = {};
uint8_t nodeRxBufferData[kNumNodes][kMaxNodeRxBufferSize * (2 + kMaxBusPacketSize)] = {};

TestTxBuffer masterNodeTxBuffer;
TestRxBuffer masterNodeRxBuffer;
TestTxBuffer nodeTxBuffer[kNumNodes];
TestRxBuffer nodeRxBuffer[kNumNodes];

uint8_t gNodeTTLs[kMaxNumNodes] = {0};

}  // namespace

TestInterface microbusMockMasterNodeBusItf;
TestInterface microbusMockNodeBusItf[kNumNodes];

void microbusMockGetConnectedNodesBitField(uint8_t connectedNodesBitfield[kMaxNumNodes / 8]) {
    for (uint32_t node = 0; node < kMaxNumNodes; node++) {
        if (gNodeTTLs[node] > 0) {
            connectedNodesBitfield[node / 8] |= (0x1 << (7 - (node % 8)));
        }
    }
}

uint32_t microbusMockGetMaxTxCredits() {
    return kMaxMasterNodeTxBufferSize;
}

// =================================== //

void microbusMockInitNetwork(bool nodeActive[]) {
    for (uint8_t n = 0; n < kMaxNumNodes; n++) {
        gNodeTTLs[n] = nodeActive[n] ? 1 : 0;
    }

    initTxBuffer(&masterNodeTxBuffer, masterNodeTxBufferData, kMaxBusPacketSize, kMaxMasterNodeTxBufferSize,
                 masterNodeTxDstNodeIds);
    initRxBuffer(&masterNodeRxBuffer, masterNodeRxBufferData, kMaxBusPacketSize, kMaxMasterNodeRxBufferSize,
                 masterNodeRxSrcNodeIds);

    microbusMockMasterNodeBusItf.txBuffer = &masterNodeTxBuffer;
    microbusMockMasterNodeBusItf.rxBuffer = &masterNodeRxBuffer;
    microbusMockMasterNodeBusItf.nodeId = 0;
    microbusMockMasterNodeBusItf.isDaemon = false;
    microbusMockMasterNodeBusItf.daemonMasterLink = false;

    for (uint32_t i = 0; i < kNumNodes; i++) {
        initTxBuffer(&nodeTxBuffer[i], nodeTxBufferData[i], kMaxBusPacketSize, kMaxNodeTxBufferSize, nullptr);
        initRxBuffer(&nodeRxBuffer[i], nodeRxBufferData[i], kMaxBusPacketSize, kMaxNodeRxBufferSize, nullptr);

        microbusMockNodeBusItf[i].txBuffer = &nodeTxBuffer[i];
        microbusMockNodeBusItf[i].rxBuffer = &nodeRxBuffer[i];
        microbusMockNodeBusItf[i].nodeId = static_cast<uint8_t>(i);
        microbusMockNodeBusItf[i].isDaemon = false;
        microbusMockNodeBusItf[i].daemonMasterLink = false;
    }
}

namespace {

void transferMasterTxToNodeRx(bool ignoreNode[kMaxNumNodes], uint8_t* numTxPacketsFreed) {
    *numTxPacketsFreed = 0;
    TestTxBuffer* txBuffer = &masterNodeTxBuffer;
    if (!circularBufferEmpty(txBuffer->start, txBuffer->end, txBuffer->size)) {
        uint8_t dstNodeId = txBuffer->txDstNodeIds[txBuffer->start];
        if (!ignoreNode[dstNodeId]) {
            TestRxBuffer* rxBuffer = &nodeRxBuffer[dstNodeId];
            *numTxPacketsFreed = (transferTxToRx(false, txBuffer, rxBuffer) ? 1 : 0);
        }
    }
}

void transferNodeTxToMasterRx(uint8_t srcNodeId) {
    TestTxBuffer* txBuffer = &nodeTxBuffer[srcNodeId];
    if (!circularBufferEmpty(txBuffer->start, txBuffer->end, txBuffer->size)) {
        TestRxBuffer* rxBuffer = &masterNodeRxBuffer;
        if (!circularBufferFull(rxBuffer->start, rxBuffer->end, rxBuffer->size)) {
            rxBuffer->rxSrcNodeIds[rxBuffer->end] = srcNodeId;
            transferTxToRx(false, txBuffer, rxBuffer);
        }
    }
}

}  // namespace

void microbusMockTransferAllTxToRx(bool ignoreNode[kMaxNumNodes], uint8_t* numTxPacketsFreed) {
    transferMasterTxToNodeRx(ignoreNode, numTxPacketsFreed);
    for (uint8_t srcNodeId = 1; srcNodeId < kNumNodes; srcNodeId++) {
        if (!ignoreNode[srcNodeId]) {
            transferNodeTxToMasterRx(srcNodeId);
        }
    }
}
