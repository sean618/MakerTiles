// Copyright (c) 2025 Sean Bremner
// Licensed under the MIT License. See LICENSE file for details.
//
// microbus.cpp - the small amount of MicroBus that isn't header-only:
//   - the weak default assertFailed() handler;
//   - printPacket(), a host-side debug dump.
//
// The logging globals (logSink/loggingEnabled/cycleIndex) are *declared* in
// diagnostics.hpp but defined by the embedding application (see the tests' main)
// so the host owns where logs go.

#include "microbus/src/diagnostics.hpp"
#include "microbus/src/packet.hpp"

#if MICROBUS_LOGGING
#include <cassert>
#endif

namespace microbus {

// Default assertion handler. Weak so an application can replace it (e.g. to
// reset the MCU). On host builds with logging it flushes and aborts; otherwise
// it spins, which on hardware halts the offending context for a debugger.
void __attribute__((weak)) assertFailed(const char* message, std::size_t messageLen) noexcept {
    (void)message;
    (void)messageLen;
#if MICROBUS_LOGGING
    if (logSink) {
        std::fflush(logSink);
    }
    assert(0 && "microbus assertion failed");
#endif
    for (;;) {
    }
}

#if MICROBUS_LOGGING
// Human-readable dump of a packet, used during bring-up. Empty packets are
// skipped unless MICROBUS_LOG_EMPTY_PACKETS is set.
void printPacket(const Packet* packet, bool isMaster, NodeId nodeId, bool isTx,
                 uint8_t numScheduled) noexcept {
    if (packet == nullptr) {
        return;
    }
    const uint16_t size = dataSize(packet);
    if (!(MICROBUS_LOG_EMPTY_PACKETS || size > 0)) {
        return;
    }
    MB_ASSERT(size < kMaxDataSize, "printPacket: data size out of range");

    if (isMaster) {
        MB_LOG("Master, %s packet:%u, size:%u, txSeqNum:%u",
               isTx ? "Tx" : "Rx",
               packet->protocolVersionAndPacketType & 0x0F, size, packet->txSeqNum);
        if (isTx) {
            MB_LOG_CONT(", dstNode:%3u, nextTxNode:", packet->master.dstNodeId);
            for (uint8_t i = 0; i < numScheduled; ++i) {
                MB_LOG_CONT("%u,", packet->master.nextTxNodeId[i]);
            }
            MB_LOG_CONT(" nextTxNodeAckSeqNum:");
            for (uint8_t i = 0; i < numScheduled; ++i) {
                MB_LOG_CONT("%u,", packet->master.nextTxNodeAckSeqNum[i]);
            }
        } else {
            MB_LOG_CONT(", srcNode:%3u, ackSeqNum:%u, bufferLevel:%u",
                        packet->node.srcNodeId, packet->node.ackSeqNum,
                        packet->node.bufferLevel);
        }
    } else {
        MB_LOG("Node:%u,            %s packet:%u, size:%u, txSeqNum:%u", nodeId,
               isTx ? "Tx" : "Rx",
               packet->protocolVersionAndPacketType & 0x0F, size, packet->txSeqNum);
        if (isTx) {
            MB_LOG_CONT(", ackSeqNum:%u, bufferLevel:%u", packet->node.ackSeqNum,
                        packet->node.bufferLevel);
        } else {
            MB_LOG_CONT(", ackSeqNum:%u", packet->master.nextTxNodeAckSeqNum[0]);
        }
    }

    if (size > 0) {
        MB_LOG_CONT(", data:");
        for (uint32_t i = 0; i < 10; ++i) {
            MB_LOG_CONT("%02x", packet->master.data[i]);
        }
    }
    MB_LOG_CONT("\n");
}
#endif

}  // namespace microbus
