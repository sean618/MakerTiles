// Copyright (c) 2025 Sean Bremner
// Licensed under the MIT License. See LICENSE file for details.

#pragma once

#include <cstdint>

#include "microbus/src/config.hpp"
#include "microbus/src/diagnostics.hpp"
#include "microbus/src/node_queue.hpp"
#include "microbus/src/packet.hpp"
#include "microbus/src/stats.hpp"
#include "microbus/src/tx_engine.hpp"

namespace microbus {

// ===========================================================================
//                              MasterTxEngine
//
// Master-side policy layered on top of the generic TxEngine. The plain TxEngine
// knows how to buffer, sequence, and ack packets per destination; this class
// adds the master's transmit *scheduling* concerns:
//
//   - it tracks which nodes currently have packets queued (activeTxNodes_) and
//     round-robins between them, so no single node starves the others;
//   - it stamps the destination node id onto each outgoing packet;
//   - it drops buffered packets and resets sequence state when a node leaves
//     or times out.
//
// It is used by both the master's tx path and its rx path (the latter to apply
// acks and advance receive sequence numbers).
// ===========================================================================
template <int MaxTxNodes, int MaxPackets>
class MasterTxEngine {
public:
    void init(NodeQueue* activeTxNodes) noexcept {
        bind(activeTxNodes);
        tx_.init();
    }

    void bind(NodeQueue* activeTxNodes) noexcept {
        activeTxNodes_ = activeTxNodes;
    }

    void clearBuffers() noexcept {
        tx_.dropAllPackets();
    }

    // Reserve a packet for the application to fill. Returns a pointer to the
    // data region, or nullptr if the buffer is full.
    uint8_t* allocateTxPacket(NodeId nodeId, NodeStats* stats) noexcept {
        (void)nodeId;  // kept for API symmetry with the node side
        // Refuse new packets once every node already has traffic queued.
        // TODO: unclear why this blocks *all* new packets when the active-tx set
        // is full rather than just throttling; revisit whether this is the
        // intended back-pressure or an over-restriction.
        if (activeTxNodes_ && activeTxNodes_->numNodes >= kMaxNodes) {
            return nullptr;
        }
        Packet* packet = tx_.allocateTxPacket();
        if (packet == nullptr) {
            stats->txBufferFull++;
            return nullptr;
        }
        return packet->master.data;
    }

    // Commit the packet reserved by allocateTxPacket().
    void submitAllocatedTxPacket(const uint8_t* dstNodeTtl, NodeId srcNodeId,
                                 NodeId dstNodeId, PacketType packetType,
                                 uint16_t dataSize) noexcept {
        // Drop the packet if the destination has already timed out.
        if (*dstNodeTtl == 0) {
            tx_.allocatedPacket_ = nullptr;
            return;
        }
        MB_ASSERT(packetType != PacketType::NodeData,
                  "master must not send NodeData packets");

        const bool txBufferWasEmpty = tx_.txBufferEmpty(dstNodeId);
        tx_.allocatedPacket_->master.dstNodeId = dstNodeId;
        tx_.submitAllocatedTxPacket(dstNodeId, packetType, dataSize);

        // First packet for this node: it becomes "active" in the round-robin.
        if (txBufferWasEmpty) {
            const bool added = activeTxNodes_->add(dstNodeId);
            MB_ASSERT(added, "activeTxNodes full (should have been caught by allocate)");
            (void)added;
            MB_LOG_TX("Master %u->%u adding to activeTxQueue (num:%u)\n",
                      srcNodeId, dstNodeId, activeTxNodes_->numNodes);
        }

        // A concurrent timer thread may have timed the node out between the TTL
        // check above and here; re-check and clean up if so.
        if (*dstNodeTtl == 0) {
            uint8_t unusedFreed = 0;
            removeNode(dstNodeId, &unusedFreed);
        }
    }

    void removeNode(NodeId nodeId, uint8_t* numTxPacketsFreed) noexcept {
        activeTxNodes_->removeIfExists(nodeId);
        *numTxPacketsFreed += static_cast<uint8_t>(tx_.dropNodePackets(nodeId));
        // Reset the window so the node restarts from a clean sequence on rejoin.
        tx_.resetNode(nodeId);
    }

    // Apply an ack arriving from `srcNodeId`. Returns the number of packets freed.
    uint8_t applyAck(NodeId srcNodeId, uint8_t ackSeqNum,
                          uint64_t* statsNumTxWindowRestarts) noexcept {
        const uint8_t numFreed =
            tx_.applyAck(srcNodeId, ackSeqNum, statsNumTxWindowRestarts);

        // Only a valid ack frees packets. If that drained the buffer, the node is
        // no longer active. (A duplicate/invalid ack frees nothing and must not
        // remove the node.)
        if (numFreed > 0 && tx_.txBufferEmpty(srcNodeId)) {
            activeTxNodes_->remove(srcNodeId);
            MB_LOG_TX("Master: srcNode:%u removed from activeTxQueue (left:%u)\n",
                      srcNodeId, activeTxNodes_->numNodes);
        }
        return numFreed;
    }

    bool rxCheckAndAdvanceSeq(NodeId srcNodeId, uint8_t packetTxSeqNum) noexcept {
        return tx_.rxCheckAndAdvanceSeq(srcNodeId, packetTxSeqNum);
    }

    // Pick the next data packet to transmit, round-robining over active nodes.
    //
    // On a single channel we prefer to send several packets in a row to one
    // destination (a "burst") so that we are not forced to alternate data/ack
    // every slot; burstSize == 1 restores strict round-robin.
    Packet* nextTxDataPacket(uint8_t burstSize) noexcept {
        if (activeTxNodes_->numNodes == 0) {
            MB_LOG_TX("Master: no active tx nodes\n");
            return nullptr;
        }

        uint32_t lastQueueIndex = lastTxQueueIndex_;
        if (lastQueueIndex >= activeTxNodes_->numNodes) {
            lastQueueIndex = activeTxNodes_->numNodes - 1;
        }

        // Continue an in-progress burst to the same destination if we can.
        if (burstSize > 1 && lastTxQueueCount_ < burstSize) {
            const NodeId dstNodeId = activeTxNodes_->getNode(lastQueueIndex);
            if (PacketEntry* entry = tx_.nextTxPacketForNode(dstNodeId)) {
                lastTxQueueCount_++;
                return &entry->packet;
            }
        }

        // Otherwise scan forward from the last index for a node with traffic.
        MB_LOG_TX("Master, nextTxDataPacket checking nodes:");
        PacketEntry* entry = nullptr;
        uint32_t i = lastQueueIndex;
        do {
            i++;
            if (i >= activeTxNodes_->numNodes) {
                i = 0;
            }
            const NodeId dstNodeId = activeTxNodes_->getNode(i);
            MB_LOG_TX_CONT("%u, ", dstNodeId);
            entry = tx_.nextTxPacketForNode(dstNodeId);
        } while (i != lastQueueIndex && entry == nullptr);
        MB_LOG_TX_CONT("\n");

        lastTxQueueIndex_ = i;
        if (entry) {
            lastTxQueueCount_ = 1;
            return &entry->packet;
        }
        return nullptr;
    }

    // ---- pass-throughs / queries ----
    uint8_t expectedRxSeq(NodeId srcNodeId) noexcept {
        return tx_.expectedRxSeq(srcNodeId);
    }
    uint8_t bufferedCount(NodeId dstNodeId) noexcept {
        return tx_.bufferedCount(dstNodeId);
    }
    uint8_t totalBuffered() noexcept {
        return tx_.totalBuffered();
    }
    int numStored() const noexcept { return tx_.numStored(); }
    int numFreed() const noexcept { return tx_.numFreed(); }

private:
    TxEngine<MaxTxNodes, MaxPackets, true> tx_{};
    NodeQueue* activeTxNodes_ = nullptr;
    uint8_t lastTxQueueIndex_ = 0;
    uint8_t lastTxQueueCount_ = 0;
};

}  // namespace microbus
