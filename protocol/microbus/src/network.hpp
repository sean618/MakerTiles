// Copyright (c) 2025 Sean Bremner
// Licensed under the MIT License. See LICENSE file for details.
//
// network.hpp - who is on the bus.
//
// Joining is a two-step handshake:
//   1. An unjoined node broadcasts a NewNodeRequest carrying its 64-bit unique
//      id during an "unallocated" slot offered by the master.
//   2. The master picks a free NodeId, remembers the (uniqueId -> nodeId)
//      mapping, and publishes it in a NewNodeResponse. The node adopts the id
//      and, once it starts transmitting under it, the master considers it fully
//      joined and forgets the pending mapping.
//
// Leaving is implicit: every device tracks a time-to-live that is refreshed
// whenever it hears from / transmits to the peer. If the TTL hits zero the peer
// is dropped. The master keeps a TTL per node; each node keeps a single TTL for
// the master.

#pragma once

#include <algorithm>
#include <array>
#include <cstdint>

#include "microbus/src/config.hpp"
#include "microbus/src/diagnostics.hpp"
#include "microbus/src/node_queue.hpp"
#include "microbus/src/packet.hpp"

namespace microbus {

// ---------------------------------------------------------------------------
// Timeouts (derived so they scale with bus size / slot time)
// ---------------------------------------------------------------------------

// A node must transmit at least this often or the master drops it. The node's
// own timeout is shorter than the master's so the node gives up first.
inline constexpr uint32_t kNodeTimeoutUs =
    2u * 2u /* allocation every 2 slots */ * kMaxSlotsBetweenServicing *
    kMaxNodes * kSlotTimeUs;
inline constexpr uint32_t kMasterTimeoutUs =
    kNodeTimeoutUs + (100u * kSlotTimeUs);

// The master stores TTLs as a 0..128 count (one byte) rather than microseconds,
// decrementing them on a coarse tick to save RAM.
inline constexpr uint8_t kMasterMaxTtl = 128;
inline constexpr uint32_t kTtlTickUs = kMasterTimeoutUs / kMasterMaxTtl;
inline constexpr uint8_t kRemoveNodeTtl = 0xFF;  // marker: "drop next sweep"

// Join backoff: a node waits a random number of slots (up to this) between
// requests so simultaneously-powered nodes don't collide forever.
inline constexpr uint8_t kMaxJoinBackoff = kMaxNodes / 2;

// ---------------------------------------------------------------------------
// NetworkManager - master side.
//
// Standard-layout / trivially default-constructible so Master can still
// zero-init it as part of a single memset in Master::init.
// ---------------------------------------------------------------------------

class NetworkManager {
public:
    void init(NodeQueue* activeNodes) noexcept { activeNodes_ = activeNodes; }

    // A node was heard from: refresh its TTL, and if it was mid-join mark the
    // join complete.
    void recordRxPacket(uint8_t nodeTtl[kMaxNodes], NodeId rxNodeId) noexcept {
        MB_ASSERT(rxNodeId < kMaxNodes, "rxNodeId out of range");
        nodeTtl[rxNodeId] = kMasterMaxTtl;
        if (numPending_ > 0 && removePending(rxNodeId)) {
            MB_LOG("Node:%u - fully joined\n", rxNodeId);
        }
    }

    // Remove a node from the pending-join table (it has fully joined or left).
    bool removePending(NodeId nodeId) noexcept {
        for (uint8_t i = 0; i < numPending_; ++i) {
            if (pendingNodeId_[i] == nodeId) {
                MB_ASSERT(pendingUniqueId_[i] > 0, "pending entry has no uniqueId");
                for (uint8_t j = i; j + 1 < numPending_; ++j) {
                    pendingUniqueId_[j] = pendingUniqueId_[j + 1];
                    pendingNodeId_[j] = pendingNodeId_[j + 1];
                }
                --numPending_;
                return true;
            }
        }
        return false;
    }

    // Allocate a free NodeId for a newly-seen uniqueId, queued for the next
    // NewNodeResponse. No-op if already pending or the network is full.
    void registerNewNode(uint8_t nodeTtl[kMaxNodes], uint64_t uniqueId,
                         uint32_t* networkFullCount) noexcept {
        MB_ASSERT(uniqueId != 0, "uniqueId must be non-zero");

        if (numPending_ == kMaxPendingJoins) {
            // TODO: surface this (a stat/log) so a persistently full pending
            // table is observable rather than silently making nodes retry.
            return;  // pending table full; the node will retry
        }
        for (uint8_t i = 0; i < numPending_; ++i) {
            if (pendingUniqueId_[i] == uniqueId) {
                return;  // already allocating one for this node
            }
        }

        NodeId nodeId = kInvalidNode;
        for (NodeId id = kFirstNode; id < kMaxNodes; ++id) {
            if (nodeTtl[id] == 0) {
                nodeId = id;
                break;
            }
        }
        if (nodeId == kInvalidNode) {
            ++(*networkFullCount);
            return;
        }

        MB_LOG_NETWORK("Master - Node:%u partial join uniqueId:0x%llx\n", nodeId,
                       (unsigned long long)uniqueId);
        // TODO: give a freshly joined node a high initial TTL here, but bring it
        // down by scheduling newly joined nodes to transmit as a priority so they
        // confirm the join quickly instead of relying on the full TTL window.
        nodeTtl[nodeId] = kMasterMaxTtl;
        pendingUniqueId_[numPending_] = uniqueId;
        pendingNodeId_[numPending_] = nodeId;
        ++numPending_;
        activeNodes_->add(nodeId);
    }

    // Coarse TTL tick (timer thread). Decrements every node's TTL when enough
    // time has accumulated, flagging expired ones for the next removal sweep.
    void updateTimeUs(uint8_t nodeTtl[kMaxNodes], uint32_t usIncr) noexcept {
        ttlAccumUs_ += usIncr;
        if (ttlAccumUs_ < kTtlTickUs) {
            return;
        }
        ttlAccumUs_ -= kTtlTickUs;
        for (NodeId id = kFirstNode; id < kMaxNodes; ++id) {
            if (nodeTtl[id] > 0 && nodeTtl[id] != kRemoveNodeTtl) {
                if (--nodeTtl[id] == 0) {
                    nodeTtl[id] = kRemoveNodeTtl;  // flag for removeAnyTimedOutNodes
                }
            }
        }
    }

    bool hasPendingNewNodes() const noexcept { return numPending_ > 0; }

    // Handle an incoming NewNodeRequest from an unjoined node.
    void rxNewNodeRequest(uint8_t nodeTtl[kMaxNodes], const Packet* packet,
                          uint32_t* networkFullCount) noexcept {
        MB_ASSERT(packet->dataSize2 == kNewNodeEntrySize, "bad new-node request size");
        const auto rx = readNewNodeEntry(&packet->node.data[0]);
        registerNewNode(nodeTtl, rx.uniqueId, networkFullCount);
    }

    // Fill `packet` with the current batch of id assignments to broadcast.
    void txNewNodeResponse(Packet* packet) noexcept {
        setVersionAndType(packet, PacketType::NewNodeResponse);
        packet->master.dstNodeId = kUnallocatedNode;

        MB_LOG_NETWORK("Master, Tx new node response:");
        const auto maxEntries =
            static_cast<uint8_t>(std::min<uint16_t>(
                numPending_, kMaxDataSize / kNewNodeEntrySize));
        uint8_t written = 0;
        for (uint8_t i = 0; i < kMaxPendingJoins; ++i) {
            if (pendingUniqueId_[i] > 0) {
                MB_LOG_NETWORK_CONT(" %u:0x%llx,", pendingNodeId_[i],
                                    (unsigned long long)pendingUniqueId_[i]);
                writeNewNodeEntry(&packet->master.data[written * kNewNodeEntrySize],
                                  NewNodeEntry{pendingUniqueId_[i], pendingNodeId_[i]});
                if (++written == maxEntries) {
                    break;
                }
            }
        }
        MB_LOG_NETWORK_CONT("\n");
        setDataSize(packet, static_cast<uint16_t>(written * kNewNodeEntrySize));
    }

    // Clear mid-join nodes from a "connected nodes" bitfield (they're not fully
    // on the network yet).
    void clearUnjoinedNodes(uint8_t bitfield[kMaxNodes / 8]) noexcept {
        for (uint8_t i = 0; i < numPending_; ++i) {
            const NodeId node = pendingNodeId_[i];
            MB_ASSERT(node != 0, "pending node id 0");
            bitfield[node / 8] &= ~(0x1 << (7 - (node % 8)));
        }
    }

private:
    std::array<uint64_t, kMaxPendingJoins> pendingUniqueId_{};
    std::array<NodeId, kMaxPendingJoins> pendingNodeId_{};
    uint8_t numPending_ = 0;
    NodeQueue* activeNodes_ = nullptr;
    uint32_t ttlAccumUs_ = 0;
};

// ---------------------------------------------------------------------------
// Node-side membership helpers (a node tracks only its link to the master).
// ---------------------------------------------------------------------------

inline void nodeRecordTxPacketSent(int32_t* ttl) noexcept {
    *ttl = static_cast<int32_t>(kNodeTimeoutUs);
}

inline void nodeUpdateTtlUs(int32_t* ttl, uint32_t usIncr) noexcept {
    if (*ttl > 0) {
        *ttl -= static_cast<int32_t>(usIncr);
    }
}

inline bool nodeHasTimedOut(int32_t ttl) noexcept { return ttl <= 0; }

// Build a NewNodeRequest advertising our unique id. Callers must rate-limit how
// often they send these: contending nodes need a randomised back-off so they
// don't collide on every join slot (Node::prepareTx applies one via its join
// countdown).
inline Packet* makeNewNodeRequest(Packet* packet, uint64_t uniqueId) noexcept {
    setVersionAndType(packet, PacketType::NewNodeRequest);
    packet->node.srcNodeId = kUnallocatedNode;
    setDataSize(packet, kNewNodeEntrySize);
    writeNewNodeEntry(&packet->node.data[0], NewNodeEntry{uniqueId, 0});
    return packet;
}

// Parse a NewNodeResponse; if it assigns *us* an id, adopt it.
inline void rxNewNodeResponse(const Packet* packet, uint64_t uniqueId,
                              NodeId* nodeId, int32_t* ttl,
                              uint32_t* statsNodeJoined) noexcept {
    const auto numEntries =
        static_cast<uint8_t>(dataSize(packet) / kNewNodeEntrySize);
    for (uint8_t i = 0; i < numEntries; ++i) {
        const auto rx = readNewNodeEntry(&packet->master.data[i * kNewNodeEntrySize]);
        if (rx.uniqueId == uniqueId) {
            *nodeId = rx.nodeId;
            ++(*statsNodeJoined);
            *ttl = static_cast<int32_t>(kNodeTimeoutUs);
            MB_LOG("Node:%u - joined uniqueId:0x%llx\n", rx.nodeId,
                   (unsigned long long)uniqueId);
            return;
        }
    }
}

}  // namespace microbus
