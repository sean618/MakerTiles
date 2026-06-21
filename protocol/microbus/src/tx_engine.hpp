// Copyright (c) 2025 Sean Bremner
// Licensed under the MIT License. See LICENSE file for details.
//
// tx_engine.hpp - reliable, in-order transmission via a sliding window.
//
// This is the heart of the protocol's reliability, and is shared by both the
// master and the nodes. It works like TCP's sliding window:
//
//   * Outgoing packets get consecutive sequence numbers and are buffered.
//   * The sender streams packets up to the window edge (kSlidingWindowSize
//     unacked packets), then pauses.
//   * The receiver acks the highest in-order sequence number it has seen; an
//     ack slides the window forward and frees the buffered packets it covers.
//   * If acks stop arriving, after kAcksBeforeWindowRestart idle acks the sender
//     rewinds to the window start and retransmits.
//
// It is cheap when loss is low and self-correcting when loss is high, and it
// guarantees the receiver sees packets exactly once, in order.
//
// Three layers, smallest first:
//   TxWindow         - per-peer window cursors (4 bytes, fits one 32-bit word)
//   TxWindowManager  - one window + expected-rx-seq per peer
//   PacketStore      - the pool of buffered outgoing packets
//   TxEngine         - ties the above together into the usable API

#pragma once

#include <algorithm>
#include <array>
#include <cstdint>

#include "microbus/src/config.hpp"
#include "microbus/src/diagnostics.hpp"
#include "microbus/src/packet.hpp"
#include "microbus/src/util.hpp"

namespace microbus {

// ---------------------------------------------------------------------------
// TxWindow - the four cursors describing one peer's window.
// ---------------------------------------------------------------------------

struct TxWindow {
    uint8_t start = 0;       // oldest unacked sequence number
    uint8_t end = 0;         // one past the newest buffered sequence number
    uint8_t next = 0;        // next sequence number to (re)transmit
    uint8_t pauseCount = 0;  // acks to wait for before restarting the window

    bool empty() const noexcept { return start == end; }
    bool wrapped() const noexcept { return end < start; }

    // Is `seq` an in-flight (buffered, not yet acked) sequence number?
    bool contains(uint8_t seq) const noexcept {
        return wrapped() ? (seq >= start || seq < end)
                         : (seq >= start && seq < end);
    }

    // Like contains() but also accepts the one-past-the-end position.
    bool isValidPosition(uint8_t seq) const noexcept {
        return wrapped() ? (seq >= start || seq <= end)
                         : (seq >= start && seq <= end);
    }

    // The largest position the window may stream to before pausing.
    uint8_t windowEnd() const noexcept {
        return static_cast<uint8_t>(start + kSlidingWindowSize);
    }

    uint8_t prevStart() const noexcept {
        return (start == 0) ? end : static_cast<uint8_t>(start - 1);
    }
};
static_assert(sizeof(TxWindow) == 4, "TxWindow should pack into one 32-bit word");

// ---------------------------------------------------------------------------
// TxWindowManager - per-peer sequence bookkeeping (no packet storage).
// ---------------------------------------------------------------------------

template <int MaxTxNodes>
class TxWindowManager {
public:
    void reset() noexcept {
        for (int n = 0; n < MaxTxNodes; ++n) {
            txWindows_[n] = TxWindow{};
            expectedRxSeq_[n] = kNoSeq;
        }
    }

    // Return one peer to a clean state (e.g. when it rejoins).
    void resetNode(NodeId node) noexcept {
        txWindows_[node] = TxWindow{};
        expectedRxSeq_[node] = kNoSeq;
    }

    // ---- transmit side ----

    bool txBufferEmpty(NodeId dst) const noexcept { return txWindows_[dst].empty(); }

    uint8_t bufferedCount(NodeId dst) const noexcept {
        const auto& w = txWindows_[dst];
        return static_cast<uint8_t>(w.end - w.start);
    }

    // Claim the next sequence number for a freshly-queued packet.
    uint8_t assignTxSeq(NodeId dst) noexcept {
        auto& w = txWindows_[dst];
        const uint8_t s = w.end;
        w.end = nextSeq(w.end);  // must be atomic w.r.t. ISR if window is shared
        return s;
    }

    // Sequence number to send next, or kNoSeq when there is nothing to send
    // right now (empty, paused, or we've hit the window edge).
    uint8_t nextTxSeq(NodeId dst) noexcept {
        auto& w = txWindows_[dst];
        if (w.pauseCount > 0 || w.empty()) {
            return kNoSeq;
        }
        MB_ASSERT(w.isValidPosition(w.next), "tx window cursor out of range");

        if (w.next == w.end || w.next == w.windowEnd()) {
            MB_LOG_TX("%s -> %u: window edge reached, pausing\n", dst != kMasterNodeId ? "Master" : "Node", dst);
            w.pauseCount = kAcksBeforeWindowRestart;
            return kNoSeq;
        }

        const uint8_t s = w.next;
        w.next = nextSeq(w.next);
        return s;
    }

    // Apply an incoming ack. `release(node, seq)` is invoked for each buffered
    // packet the ack frees.
    template <typename ReleaseFn>
    void processAck(uint8_t ackSeq, NodeId src, ReleaseFn&& release,
                    uint64_t* statsWindowRestarts) noexcept {
        if (ackSeq == kNoSeq) {
            return;  // peer hasn't acked any data yet
        }
        auto& w = txWindows_[src];

        if (!w.contains(ackSeq)) {
            // A stale/duplicate ack. If we were paused waiting for acks, count
            // it down; reaching zero means "give up waiting, retransmit".
            if (ackSeq != w.prevStart()) {
                MB_LOG_TX("%s - src:%u stale ack start:%u end:%u got:%u\n",
                          srcNode == kMasterNodeId ? "Master" : "Node", src, w.start, w.end, ackSeq);
            }
            if (w.pauseCount > 0) {
                if (--w.pauseCount == 0) {
                    MB_LOG_TX("%s -> %u: restarting window\n", srcNode == kMasterNodeId ? "Master" : "Node", src);
                    ++(*statsWindowRestarts);
                    w.next = w.start;
                }
            }
            return;
        }

        const uint8_t newStart = nextSeq(ackSeq);
        for (uint8_t seq = w.start; seq != newStart; seq = nextSeq(seq)) {
            release(src, seq);
            if (seq == w.next) {
                w.next = newStart;  // freed what we were about to send; skip ahead
            }
        }
        w.start = newStart;
        w.pauseCount = 0;
    }

    // ---- receive side ----

    uint8_t expectedRxSeq(NodeId src) const noexcept { return expectedRxSeq_[src]; }

    // Validate an incoming packet's sequence number against what we expect, and
    // advance on a match. Returns whether the packet was the expected next one.
    bool rxCheckAndAdvance(uint8_t packetSeq, NodeId src) noexcept {
        const uint8_t prev = expectedRxSeq_[src];
        const uint8_t expected = (prev == kNoSeq) ? 0 : nextSeq(prev);
        if (packetSeq == expected) {
            expectedRxSeq_[src] = packetSeq;
            return true;
        }
        MB_LOG_TX("%s - src:%u bad rx seq exp:%u got:%u\n", srcNode == kMasterNodeId ? "Master" : "Node", src,
                  expected, packetSeq);
        return false;
    }

private:
    std::array<uint8_t, MaxTxNodes> expectedRxSeq_{};
    std::array<TxWindow, MaxTxNodes> txWindows_{};
};

// ---------------------------------------------------------------------------
// PacketStore - a flat pool of buffered outgoing packets.
//
// Lookups are linear because the pools are small (a handful of in-flight
// packets) and this keeps the structure trivially copyable / interrupt-safe.
// ---------------------------------------------------------------------------

template <int MaxPackets, bool IsMaster>
class PacketStore {
public:
    Packet* allocate() noexcept {
        for (auto& e : entries_) {
            if (!e.inUse) {
                e.inUse = true;  // single store: atomic w.r.t. ISR/main
                ++numStored_;
                return &e.packet;
            }
        }
        return nullptr;
    }

    // Find a buffered packet by destination (master only) and sequence number.
    PacketEntry* find(NodeId dst, uint8_t seq) noexcept {
        for (auto& e : entries_) {
            const bool dstMatches = IsMaster ? (e.packet.master.dstNodeId == dst) : true;
            if (e.inUse && dstMatches && e.packet.txSeqNum == seq) {
                return &e;
            }
        }
        MB_ASSERT(false, "PacketStore::find missed");
        return nullptr;
    }

    bool free(NodeId dst, uint8_t seq) noexcept {
        auto* e = find(dst, seq);
        if (e == nullptr) {
            return false;
        }
        ++numFreed_;
        e->inUse = false;
        return true;
    }

    int inUseCount() const noexcept {
        int n = 0;
        for (const auto& e : entries_) {
            if (e.inUse) ++n;
        }
        return n;
    }

    // Drop everything destined for one node (master side). Returns count freed.
    int dropForNode(NodeId dst) noexcept {
        int n = 0;
        for (auto& e : entries_) {
            if (e.inUse && e.packet.master.dstNodeId == dst) {
                e.inUse = false;
                ++numFreed_;
                ++n;
            }
        }
        return n;
    }

    void dropAll() noexcept {
        for (auto& e : entries_) {
            if (e.inUse) {
                e.inUse = false;
                ++numFreed_;
            }
        }
    }

    // Diagnostic counters: at quiescence stored == freed.
    int numStored() const noexcept { return numStored_; }
    int numFreed() const noexcept { return numFreed_; }

private:
    std::array<PacketEntry, MaxPackets> entries_{};
    int numStored_ = 0;
    int numFreed_ = 0;
};

// ---------------------------------------------------------------------------
// TxEngine - the public transmit API: allocate, submit, fetch-next, ack.
// ---------------------------------------------------------------------------

template <int MaxTxNodes, int MaxPackets, bool IsMaster>
class TxEngine {
public:
    void init() noexcept {
        maxBufferLevel_ = 0;
        allocatedPacket_ = nullptr;
        store_.dropAll();
        windows_.reset();
    }

    // ---- producer side (may be a separate, lower-priority thread) ----

    // Reserve a buffer slot for the application to fill. Returns nullptr if full.
    // The reserved packet must be submitted before the next allocate.
    Packet* allocateTxPacket() noexcept {
        Packet* p = store_.allocate();
        if (p == nullptr) {
            return nullptr;
        }
        MB_ASSERT(allocatedPacket_ == nullptr,
                  "previous allocated packet was not submitted");
        allocatedPacket_ = p;
        return p;
    }

    // Hand the reserved packet to the engine: stamp its header and give it a
    // sequence number, putting it in flight.
    void submitAllocatedTxPacket(NodeId dst, PacketType type, uint16_t size) noexcept {
        Packet* p = allocatedPacket_;
        MB_ASSERT(p != nullptr, "no allocated packet to submit");
        MB_ASSERT(dst < kMaxNodes, "dst out of range");
        MB_ASSERT(size <= kMaxDataSize, "tx packet too large");
        if (p == nullptr || dst >= kMaxNodes || size > kMaxDataSize) {
            return;
        }
        setVersionAndType(p, type);
        setDataSize(p, size);
        p->txSeqNum = windows_.assignTxSeq(dst);
        allocatedPacket_ = nullptr;
        maxBufferLevel_ = std::max(maxBufferLevel_, windows_.bufferedCount(dst));
    }

    // ---- consumer side (the slot scheduler) ----

    // Next buffered packet to send to `dst`, or nullptr if nothing is due.
    PacketEntry* nextTxPacketForNode(NodeId dst) noexcept {
        const uint8_t seq = windows_.nextTxSeq(dst);
        if (seq == kNoSeq) {
            return nullptr;
        }
        PacketEntry* e = store_.find(dst, seq);
        MB_ASSERT(e != nullptr, "scheduled packet not found in store");
        return e;
    }

    // ---- receive side ----

    bool rxCheckAndAdvanceSeq(NodeId src, uint8_t packetSeq) noexcept {
        return windows_.rxCheckAndAdvance(packetSeq, src);
    }

    uint8_t expectedRxSeq(NodeId src) const noexcept {
        return windows_.expectedRxSeq(src);
    }

    // Apply an incoming ack, freeing the packets it covers. Returns count freed.
    uint8_t applyAck(NodeId src, uint8_t ackSeq,
                          uint64_t* statsWindowRestarts) noexcept {
        uint8_t freed = 0;
        auto release = [this, &freed](NodeId node, uint8_t seq) {
            const bool ok = store_.free(node, seq);
            MB_ASSERT(ok, "applyAck: free failed");
            if (ok) ++freed;
        };
        windows_.processAck(ackSeq, src, release, statsWindowRestarts);
        return freed;
    }

    // ---- queries / maintenance ----

    uint8_t bufferedCount(NodeId dst) const noexcept { return windows_.bufferedCount(dst); }
    bool txBufferEmpty(NodeId dst) const noexcept { return windows_.txBufferEmpty(dst); }
    uint8_t totalBuffered() const noexcept { return static_cast<uint8_t>(store_.inUseCount()); }

    void resetNode(NodeId node) noexcept { windows_.resetNode(node); }
    void dropAllPackets() noexcept { store_.dropAll(); }
    int dropNodePackets(NodeId node) noexcept { return store_.dropForNode(node); }

    int numStored() const noexcept { return store_.numStored(); }
    int numFreed() const noexcept { return store_.numFreed(); }

    // Read/written directly by node/master tx-prep code that fills the payload
    // between allocate and submit.
    Packet* allocatedPacket_ = nullptr;

private:
    TxWindowManager<MaxTxNodes> windows_{};
    PacketStore<MaxPackets, IsMaster> store_{};
    uint8_t maxBufferLevel_ = 0;
};

}  // namespace microbus
