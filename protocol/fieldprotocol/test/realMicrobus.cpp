/*
 * Copyright © 2024 - 2024 Sean Bremner. All rights reserved.>>
 */

// ============================================================== //
// Real microbus network configuration. Compiled only when linking against the
// external microbus library (i.e. not in the mock test build).
// ============================================================== //

#include "../src/fpCommon.hpp"

#ifndef USE_MOCK_MICROBUS

#include <cassert>
#include <cstdint>
#include <cstring>

#include "../src/fpDaemon.hpp"
#include "../src/fpMaster.hpp"
#include "../src/fpNode.hpp"
#include "realMicrobus.hpp"

#include "../../microbus/src/master.hpp"
#include "../../microbus/src/microbus.hpp"
#include "../../microbus/src/node.hpp"

using namespace fp;
using namespace microbus;

namespace microbus {
// Diagnostics globals declared in diagnostics.hpp; the embedding application
// owns them, and for the system test this adapter is that host.
std::FILE* logSink = nullptr;
bool loggingEnabled = false;
uint64_t cycleIndex = 0;
}  // namespace microbus

namespace {

constexpr uint8_t kMaxMasterNodeTxBufferSize = 5;
constexpr uint8_t kMaxMasterNodeRxBufferSize = 6;
constexpr uint8_t kMaxNodeRxBufferSize = 6;

microbus::Master<kMaxMasterNodeRxBufferSize> microbusMaster;
microbus::Node<kMaxNodeRxBufferSize> microbusNode[kMaxNodes];
bool activeNodes[kMaxNodes];

}  // namespace

MicrobusMasterInterface microbusMasterNodeItf;
MicrobusNodeInterface microbusNodeItf[kMaxNodes];

// ---- Master interface ---- //
uint8_t* MicrobusMasterInterface::allocateTxPacket() {
    return microbusMaster.allocateTxPacket();
}
void MicrobusMasterInterface::submitTxPacket(uint8_t dstNodeId, uint16_t numBytes) {
    microbusMaster.submitTxPacket(dstNodeId, numBytes);
}
uint8_t* MicrobusMasterInterface::peekRxPacket(uint16_t& numBytes, uint8_t& srcNodeId) {
    return microbusMaster.peekRxData(&numBytes, &srcNodeId);
}
bool MicrobusMasterInterface::popRxPacket() {
    return microbusMaster.popRxData();
}

// ---- Node interface ---- //
uint8_t* MicrobusNodeInterface::allocateTxPacket() {
    return static_cast<microbus::Node<kMaxNodeRxBufferSize>*>(node)->allocateTxPacket();
}
void MicrobusNodeInterface::submitTxPacket(uint8_t dstNodeId, uint16_t numBytes) {
    static_cast<microbus::Node<kMaxNodeRxBufferSize>*>(node)->submitTxPacket(dstNodeId, numBytes);
}
uint8_t* MicrobusNodeInterface::peekRxPacket(uint16_t& numBytes, uint8_t& srcNodeId) {
    return static_cast<microbus::Node<kMaxNodeRxBufferSize>*>(node)->peekRxData(&numBytes, &srcNodeId);
}
bool MicrobusNodeInterface::popRxPacket() {
    return static_cast<microbus::Node<kMaxNodeRxBufferSize>*>(node)->popRxData();
}
uint8_t MicrobusNodeInterface::getNodeId() const {
    return static_cast<microbus::Node<kMaxNodeRxBufferSize>*>(node)->nodeId;
}

uint32_t microbusGetMaxTxCredits() {
    return kMaxMasterNodeTxBufferSize;
}

void microbusInitNetwork(bool nodeActive[]) {
    uint8_t numTxNodesScheduled = 1;
    microbusMaster.init(numTxNodesScheduled);

    activeNodes[0] = false;
    for (uint8_t n = 1; n < kMaxNumNodes; n++) {
        activeNodes[n] = nodeActive[n];
        if (nodeActive[n]) {
            uint64_t uniqueId = fpTestRand();
            uniqueId = (uniqueId << 32) | fpTestRand();
            microbusNode[n].init(uniqueId);
            microbusNodeItf[n].node = &microbusNode[n];
            microbusNodeItf[n].isDaemon = false;
            microbusNodeItf[n].daemonMasterLink = false;
        }
    }
}

// ===================================== //

namespace {
const Packet nullPacket = {0};
}

void microbusTransferAllTxToRx(bool ignoreNode[kMaxNumNodes], uint8_t* numTxPacketsFreed) {
    microbus::cycleIndex++;
    bool allowNodeTxOverlaps = true;

    microbusMaster.updateTimeUs(kSlotTimeUs);
    Packet* masterRxPacket;
    Packet* masterTxPacket;
    microbusMaster.fullDuplexBeginSlot(&masterTxPacket, &masterRxPacket, false);
    *numTxPacketsFreed = microbusMaster.fullDuplexEndSlot();

    Packet* nodeTxPacket = nullptr;
    uint32_t numNodeTxPackets = 0;
    uint8_t lastTxN = 0;

    for (uint8_t n = 1; n < kMaxNumNodes; n++) {
        if (activeNodes[n]) {
            Packet* txPacket;
            Packet* rxPacket;
            microbusNode[n].updateTimeUs(kSlotTimeUs);
            microbusNode[n].fullDuplexBeginSlot(&txPacket, &rxPacket, false);
            if (txPacket != nullptr) {
                if (numNodeTxPackets > 0 && kMicrobusLogging) {
                    MB_LOG("Overlap occurred between idx:%u and %u\n", lastTxN, n);
                }
                lastTxN = n;
                numNodeTxPackets++;
                if (!ignoreNode[n]) {
                    nodeTxPacket = txPacket;
                }
            }
            microbusNode[n].fullDuplexEndSlot();
            std::memcpy(rxPacket, (masterTxPacket && !ignoreNode[n]) ? masterTxPacket : &nullPacket,
                        sizeof(Packet));
        }
    }

    if (numNodeTxPackets > 1) {
        if (allowNodeTxOverlaps) {
            nodeTxPacket = nullptr;
        } else {
            assert(0);
        }
    }
    std::memcpy(masterRxPacket, nodeTxPacket ? nodeTxPacket : &nullPacket, sizeof(Packet));
}

void microbusGetConnectedNodesBitField(uint8_t connectedNodesBitfield[kMaxNodes / 8]) {
    microbusMaster.getConnectedNodes(connectedNodesBitfield);
}

void microbusNodeUpdateTimeUs(MicrobusNodeInterface& itf, uint32_t usIncr) {
    static_cast<microbus::Node<kMaxNodeRxBufferSize>*>(itf.node)->updateTimeUs(usIncr);
}

#endif
