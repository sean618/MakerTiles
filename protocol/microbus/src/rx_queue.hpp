// Copyright (c) 2025 Sean Bremner
// Licensed under the MIT License. See LICENSE file for details.
//
// rx_queue.hpp - inbound packet buffering.
//
// Two parts working together:
//   * a flat pool of PacketEntry slots that incoming DMA writes land in, and
//   * a FIFO of pointers to the slots that turned out to hold application data,
//     waiting to be read out by the application thread.
//
// The split lets the time-critical rx path grab a free slot to receive into
// without touching the application's read queue. When a node leaves, its queued
// packets are invalidated in place (tombstoned) rather than shifted out, so the
// rx path never has to do an O(n) compaction.

#pragma once

#include <array>
#include <cstdint>
#include <cstring>

#include "microbus/src/diagnostics.hpp"
#include "microbus/src/packet.hpp"
#include "microbus/src/ring_buffer.hpp"

namespace microbus {

template <int MaxPackets>
class RxQueue {
public:
    static_assert(MaxPackets > 2, "RxQueue needs at least 3 slots");

    void init() noexcept {
        std::memset(pool_.data(), 0, sizeof(pool_));
        queue_.clear();
        maxLevel_ = 0;
    }

    // Reserve a free pool slot to receive the next packet into. Returns nullptr
    // if the completed-data queue is full (back-pressure on the application).
    PacketEntry* findFreePacket() noexcept {
        if (queue_.full()) {
            return nullptr;
        }
        for (auto& e : pool_) {
            if (!e.inUse) {
                e.inUse = true;
                return &e;
            }
        }
        return nullptr;
    }

    // Hand a received data packet to the application queue (keeps it in use).
    void addDataPacket(PacketEntry* entry) noexcept {
        queue_.push(entry);
        if (queue_.size() > maxLevel_) {
            maxLevel_ = queue_.size();
        }
    }

    // Invalidate every queued packet from a node that has left (tombstone).
    void removeAllPackets(NodeId nodeId) noexcept {
        queue_.forEachLive([nodeId](PacketEntry*& slot) {
            if (slot != nullptr && slot->packet.node.srcNodeId == nodeId) {
                slot->inUse = false;
                slot = nullptr;
            }
        });
    }

    // ---- application read API ----

    // Peek the next data packet without removing it, skipping any tombstones
    // that have reached the front. Returns nullptr when none are ready.
    Packet* peekNextDataPacket() noexcept {
        while (!queue_.empty()) {
            PacketEntry* entry = *queue_.front();
            if (entry == nullptr) {
                queue_.pop();  // discard a tombstone and keep looking
            } else {
                return &entry->packet;
            }
        }
        return nullptr;
    }

    // Release the front data packet back to the pool. Call only after a
    // successful peek (so the front is known non-null).
    bool popNextDataPacket() noexcept {
        PacketEntry** front = queue_.front();
        MB_ASSERT(front != nullptr && *front != nullptr, "pop with no ready packet");
        if (front == nullptr || *front == nullptr) {
            return false;
        }
        (*front)->inUse = false;
        queue_.pop();
        return true;
    }

private:
    std::array<PacketEntry, MaxPackets> pool_{};
    RingBuffer<PacketEntry*, MaxPackets> queue_{};
    uint8_t maxLevel_ = 0;
};

}  // namespace microbus
