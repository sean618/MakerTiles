/*
 * Copyright © 2024 - 2024 Sean Bremner. All rights reserved.>>
 */

#pragma once

#include <cstdint>

#include "../src/fpCommon.hpp"

#ifndef USE_MOCK_MICROBUS

#include "../../microbus/src/microbus.hpp"
#include "../../microbus/src/node.hpp"

#include "testCommon.hpp"

// Adapts the real microbus Master/Node transports to fp::FpInterface. The
// concrete classes are templated on the rx buffer size, so the interface holds
// a type-erased pointer plus thunks supplied at init time.
class MicrobusMasterInterface : public fp::FpInterface {
public:
    uint8_t* allocateTxPacket() override;
    void submitTxPacket(uint8_t dstNodeId, uint16_t packetSize) override;
    uint8_t* peekRxPacket(uint16_t& numBytes, uint8_t& srcNodeId) override;
    bool popRxPacket() override;
};

class MicrobusNodeInterface : public fp::FpInterface {
public:
    uint8_t* allocateTxPacket() override;
    void submitTxPacket(uint8_t dstNodeId, uint16_t packetSize) override;
    uint8_t* peekRxPacket(uint16_t& numBytes, uint8_t& srcNodeId) override;
    bool popRxPacket() override;
    uint8_t getNodeId() const override;

    void* node = nullptr;  // Node<kMaxNodeRxBufferSize>*
};

extern MicrobusMasterInterface microbusMasterNodeItf;
extern MicrobusNodeInterface microbusNodeItf[fp::kMaxNumNodes];

void microbusInitNetwork(bool nodeActive[]);
uint32_t microbusGetMaxTxCredits();
void microbusTransferAllTxToRx(bool ignoreNode[fp::kMaxNumNodes], uint8_t* numTxPacketsFreed);
void microbusGetConnectedNodesBitField(uint8_t connectedNodesBitfield[microbus::kMaxNodes / 8]);
void microbusNodeUpdateTimeUs(MicrobusNodeInterface& itf, uint32_t usIncr);

#endif
