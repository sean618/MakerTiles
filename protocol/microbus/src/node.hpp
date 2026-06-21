// Copyright (c) 2025 Sean Bremner
// Licensed under the MIT License. See LICENSE file for details.

#pragma once

#include <cstdint>
#include <array>

#include "microbus/src/config.hpp"
#include "microbus/src/diagnostics.hpp"
#include "microbus/src/network.hpp"
#include "microbus/src/packet.hpp"
#include "microbus/src/rx_queue.hpp"
#include "microbus/src/stats.hpp"
#include "microbus/src/tx_engine.hpp"
#include "microbus/src/util.hpp"

namespace microbus {

// ===========================================================================
//                                  Node
//
// A non-master device on the bus. A node only ever talks to the master, so its
// transmit engine tracks a single destination (the master). Responsibilities:
//
//   - join the network (request an id, adopt the one the master assigns);
//   - in each slot, receive the master's packet, read the broadcast schedule,
//     ack what it received, and - when the schedule says it is this node's turn
//     - transmit one buffered packet (or an empty keep-alive);
//   - hand received application data up to the caller; surface a tx buffer for
//     the caller to fill.
//
// Two physical link styles are supported, mirroring the master:
//   - full duplex (pipelined): beginSlot() prepares tx/rx for the slot about to
//     start; endSlot() does the slower processing of what was exchanged while
//     the next slot is already in flight.
//   - half duplex: the link is tx OR rx in a slot, so isTxSlot() picks, and
//     the matching halfDuplexTransmit()/halfDuplexReceive() is called.
//
// MaxRxPacketEntries sizes the receive queue at compile time.
// ===========================================================================
// MaxRxPacketEntries sizes the receive queue; MaxTxPackets sizes the per-node
// tx window buffer. MaxTxPackets must be >= kSlidingWindowSize (a node can have
// that many packets in flight). It defaults to 10 to preserve existing
// behaviour, but RAM-constrained node boards should set it to their
// MAX_MICROBUS_TX_PACKETS (typically kSlidingWindowSize == 4): the original C
// node buffered only that many, and the extra packets here cost ~192 B each.
template <int MaxRxPacketEntries, int MaxTxPackets = 10>
class Node {
    static_assert(MaxTxPackets >= kSlidingWindowSize,
                  "node tx depth must cover the sliding window");
public:
    // Exposed for the host test harness / application introspection.
    NodeId nodeId = 0;
    int32_t timeToLive = 0;

    static_assert(MaxRxPacketEntries > 2, "node rx queue needs at least 3 slots");

    // ---- full duplex (pipelined) -----------------------------------------

    // Quick, runs just before the slot: validate the previous rx, advance the
    // schedule, expose the rx buffer, and (if it is our turn) the tx packet.
    void fullDuplexBeginSlot(Packet** txPacket, Packet** rxBufferOut, bool crcError) noexcept {
        if (!initialised_) {
            return;
        }
        quickProcessPrevRx(crcError);
        // Apply the schedule the master just sent; doing it here keeps us a slot
        // ahead, in lockstep with the master's own bookkeeping.
        advanceSchedule();
        *rxBufferOut = &nextRxEntry_->packet;

        if (currentTxNode_ == nodeId && nextTxPacket_ != nullptr) {
            *txPacket = takeTxPacket();
        }
    }

    // Slower, runs after the slot's data has been exchanged.
    void fullDuplexEndSlot() noexcept {
        if (!initialised_) {
            return;
        }
        checkIfTimedOut();
        processRx();
        prepareTx();
    }

    // ---- half duplex ------------------------------------------------------

    // Whether this slot is ours to transmit in. Also advances time/schedule.
    bool isTxSlot() noexcept {
        if (!initialised_) {
            return false;
        }
        const bool isTx = (currentTxNode_ == nodeId);
        checkIfTimedOut();
        advanceSchedule();
        return isTx;
    }

    // Buffer the link layer should fill with the incoming packet before
    // calling halfDuplexReceive().
    Packet* rxBuffer() noexcept {
        return &nextRxEntry_->packet;
    }

    void halfDuplexReceive(bool crcError) noexcept {
        if (!initialised_) {
            return;
        }
        quickProcessPrevRx(crcError);
        processRx();
    }

    void halfDuplexTransmit(Packet** txPacket) noexcept {
        if (!initialised_) {
            return;
        }
        prepareTx();
        *txPacket = takeTxPacket();
    }

    // ---- time (any thread) -----------------------------------------------

    void updateTimeUs(uint32_t usIncr) noexcept {
#if MICROBUS_LOGGING
        const int32_t prevTtl = timeToLive;
#endif
        nodeUpdateTtlUs(&timeToLive, usIncr);
#if MICROBUS_LOGGING
        if (!nodeHasTimedOut(prevTtl) && nodeHasTimedOut(timeToLive)) {
            MB_LOG("Node:%u, timed out: %d\n", nodeId, timeToLive);
        }
#endif
    }

    // ---- lifecycle (main thread) -----------------------------------------

    void init(uint64_t uniqueId) noexcept {
        nodeId = kUnallocatedNode;
        uniqueId_ = uniqueId;
        timeToLive = 0;
        currentTxNode_ = kMasterNode;
        nextTxNodeId_.fill(kMasterNode);
        sentNewNodeRequest_ = false;
        joinCountdown_ = 0;
        savedRxAckValid_ = false;
        savedRxAck_ = 0;
        validRxSeqNum_ = false;
        validRxPacket_ = false;
        stats_ = NodeStats{};
        emptyPacketCycle_ = 0;
        tmpEmptyPacket_ = {};
        tmpPacket_ = {};

        rxQueue_.init();
        nextRxEntry_ = rxQueue_.findFreePacket();
        MB_ASSERT(nextRxEntry_ != nullptr, "node rx queue empty after init");
        prevRxEntry_ = nullptr;

        // reset() seeds the rx sequence numbers to kNoSeq (255, not 0).
        txEngine_.init();

        // Start out holding a harmless empty packet.
        nextTxPacket_ = &tmpPacket_;
        nextTxPacket_->protocolVersionAndPacketType = 0;
        nextTxPacket_->dataSize1 = 0;
        nextTxPacket_->dataSize2 = 0;
        nextTxPacket_->node.srcNodeId = 0;

        // Seed the join-backoff RNG from the unique id so nodes powering up
        // together don't collide forever.
        lcg_.reseed(static_cast<uint32_t>((uniqueId & 0xFFFFFFFF) ^ (uniqueId >> 32)));

        initialised_ = true;
    }

    void reset() noexcept { init(uniqueId_); }

    // ---- application API --------------------------------------------------

    // Reserve a tx packet; returns its data region or nullptr if the buffer is
    // full. Must submitTxPacket() before the next allocate.
    uint8_t* allocateTxPacket() noexcept {
        Packet* packet = txEngine_.allocateTxPacket();
        if (packet == nullptr) {
            stats_.txBufferFull++;
            return nullptr;
        }
        return packet->node.data;
    }

    // dstNodeId is ignored: a node only ever transmits to the master.
    void submitTxPacket(NodeId dstNodeId, uint16_t numBytes) noexcept {
        (void)dstNodeId;
        txEngine_.allocatedPacket_->node.srcNodeId = nodeId;
        txEngine_.submitAllocatedTxPacket(kMasterNode, PacketType::NodeData, numBytes);
    }

    Packet* peekRxPacket() noexcept {
        return rxQueue_.peekNextDataPacket();
    }

    uint8_t* peekRxData(uint16_t* size, NodeId* srcNodeId) noexcept {
        Packet* packet = peekRxPacket();
        if (packet == nullptr) {
            return nullptr;
        }
        *size = dataSize(packet);
        MB_ASSERT(*size > 0, "rx data packet with zero size");
        *srcNodeId = kMasterNode;
        return packet->master.data;
    }

    bool popRxData() noexcept {
        return rxQueue_.popNextDataPacket();
    }

    // Buffered tx packet count (to the master). Used by tests.
    uint8_t txBuffered() const noexcept {
        return txEngine_.bufferedCount(kMasterNode);
    }

private:
    // ---- receive ----------------------------------------------------------

    // Fast pass over the just-received packet: read the schedule and any ack
    // for us, validate, and check the data sequence number so we can ack it
    // immediately next slot. Defers the slower handling to processRx().
    void quickProcessPrevRx(bool rxCrcError) noexcept {
        if (!initialised_) {
            return;
        }
        prevRxEntry_ = nextRxEntry_;
        Packet* rxPacket = &prevRxEntry_->packet;
        validRxPacket_ = false;
        savedRxAckValid_ = false;

        // New-node request slots carry their own CRC (several nodes may speak).
        if (rxCrcError) {
            stats_.rxCrcFailures++;
            return;
        }

        // 0x00 / 0xFF version+type bytes are just an empty/idle packet.
        if (rxPacket->protocolVersionAndPacketType == 0 ||
            rxPacket->protocolVersionAndPacketType == 255) {
            return;
        }

        if (protocolVersion(rxPacket) != kProtocolVersion) {
            stats_.rxInvalidProtocol++;
            return;
        }

        if (packetType(rxPacket) == PacketType::MasterReset) {
            removeFromNetwork();
            return;
        }

        // Record the broadcast schedule even if this packet is not for us.
        for (uint8_t i = 0; i < kMaxScheduledSlots; ++i) {
            nextTxNodeId_[i] = rxPacket->master.nextTxNodeId[i];
        }

        // Record any ack addressed to us, again regardless of dst.
        if (nodeId != kUnallocatedNode) {
            for (uint8_t i = 0; i < kMaxScheduledSlots; ++i) {
                if (rxPacket->master.nextTxNodeId[i] == nodeId) {
                    savedRxAckValid_ = true;
                    savedRxAck_ = rxPacket->master.nextTxNodeAckSeqNum[i];
                }
            }
        }

        // Past here we only care if the packet is addressed to us.
        if (rxPacket->master.dstNodeId != nodeId) {
            return;
        }

        validRxPacket_ = true;
        stats_.rxValid++;

        // We will likely store this packet, so grab a fresh buffer for next rx.
        PacketEntry* freeEntry = rxQueue_.findFreePacket();
        if (freeEntry) {
            nextRxEntry_ = freeEntry;
            validRxSeqNum_ = true;
            if (nodeId != kUnallocatedNode &&
                packetType(rxPacket) == PacketType::MasterData) {
                validRxSeqNum_ =
                    txEngine_.rxCheckAndAdvanceSeq(kMasterNode, rxPacket->txSeqNum);
            }
        } else {
            stats_.rxBufferFull++;
            validRxPacket_ = false;  // reuse this buffer
        }
        MB_ASSERT(nextRxEntry_ != nullptr, "node lost its rx buffer");
    }

    void processRx() noexcept {
        if (savedRxAckValid_) {
            txEngine_.applyAck(kMasterNode, savedRxAck_, &stats_.txWindowRestarts);
        }
        if (validRxPacket_) {
            processRxData(prevRxEntry_);
        }
    }

    void processRxData(PacketEntry* packetEntry) noexcept {
        Packet* packet = &packetEntry->packet;
        const PacketType type = packetType(packet);
        bool packetStored = false;
        stats_.rxPacketEntries++;

        if (nodeId == kUnallocatedNode) {
            if (type == PacketType::NewNodeResponse && sentNewNodeRequest_) {
                stats_.newNodeAllocatedRx++;
                rxNewNodeResponse(packet, uniqueId_, &nodeId, &timeToLive,
                                  &stats_.nodeJoinedNw);
            }
        } else if (packet->master.dstNodeId == nodeId) {
            switch (type) {
                case PacketType::MasterData:
                    MB_ASSERT(dataSize(packet) > 0, "master data packet with zero size");
                    if (validRxSeqNum_) {
                        stats_.rxDataPackets++;
                        rxQueue_.addDataPacket(packetEntry);
                        packetStored = true;
                    }
                    break;
                default:
                    stats_.rxInvalidPacketType++;
                    break;
            }
        }

        if (!packetStored) {
            packetEntry->inUse = false;
        }
    }

    // ---- schedule ---------------------------------------------------------

    // Shift the schedule window down one slot; slot 0 becomes "whose turn now".
    void advanceSchedule() noexcept {
        currentTxNode_ = nextTxNodeId_[0];
        for (uint8_t i = 0; i + 1 < kMaxScheduledSlots; ++i) {
            nextTxNodeId_[i] = nextTxNodeId_[i + 1];
        }
        nextTxNodeId_[kMaxScheduledSlots - 1] = kInvalidNode;
    }

    // ---- transmit ---------------------------------------------------------

    // Decide what to queue for our next turn: a buffered data packet, an empty
    // keep-alive, or (when unjoined) a new-node request after a backoff.
    void prepareTx() noexcept {
        // If we still hold an un-sent data/join packet, keep trying to send it.
        if (nextTxPacket_) {
            const PacketType t = packetType(nextTxPacket_);
            if (t == PacketType::NodeData || t == PacketType::NewNodeRequest) {
                return;
            }
        }

        Packet* txPacket = nullptr;

        if (nodeId != kUnallocatedNode) {
            if (!txEngine_.txBufferEmpty(kMasterNode)) {
                if (PacketEntry* entry = txEngine_.nextTxPacketForNode(kMasterNode)) {
                    entry->packet.node.ackSeqNum = txEngine_.expectedRxSeq(kMasterNode);
                    txPacket = &entry->packet;
                }
            }

            if (txPacket == nullptr) {
                // Nothing to send, but we must transmit something on our turn.
                // Alternate between two empty stubs because the DMA reads the
                // buffer a slot later than we fill it.
                emptyPacketCycle_ = static_cast<uint8_t>((emptyPacketCycle_ + 1) % 2);
                PacketStub* stub = &tmpEmptyPacket_[emptyPacketCycle_];
                setVersionAndType(stub, PacketType::NodeEmpty);
                setDataSize(stub, 0);
                stub->node.srcNodeId = nodeId;
                stub->txSeqNum = kNoSeq;
                stub->node.ackSeqNum = txEngine_.expectedRxSeq(kMasterNode);
                // The DMA over-reads past the stub into adjacent memory; the
                // receiver ignores it (data size 0).
                txPacket = reinterpret_cast<Packet*>(stub);
            }

            if (txPacket != nullptr) {
                txPacket->node.bufferLevel = txEngine_.bufferedCount(kMasterNode);
            }
        } else {
            // Unjoined: send a new-node request when our backoff expires.
            if (joinCountdown_ == 0) {
                stats_.newNodeRequest++;
                sentNewNodeRequest_ = true;
                txPacket = makeNewNodeRequest(&tmpPacket_, uniqueId_);
                joinCountdown_ = static_cast<uint8_t>(lcg_.next() % kMaxJoinBackoff);
                MB_LOG_NETWORK("Node %u, Tx new-node request: 0x%llx (backoff:%u)\n",
                               nodeId, (unsigned long long)uniqueId_, joinCountdown_);
            } else {
                joinCountdown_--;
            }
        }

        nextTxPacket_ = txPacket;
    }

    // Refresh the ack on the queued packet just before sending it.
    void refreshTxAck() noexcept {
        if (nextTxPacket_) {
            nextTxPacket_->node.ackSeqNum = txEngine_.expectedRxSeq(kMasterNode);
        }
    }

    // Hand the queued packet to the link layer, finalising acks and stats.
    Packet* takeTxPacket() noexcept {
        if (nextTxPacket_ == nullptr) {
            return nullptr;
        }

        // Throttle new-node requests behind a backoff.
        if (nodeId == kUnallocatedNode) {
            if (joinCountdown_ == 0) {
                stats_.newNodeRequest++;
                joinCountdown_ = static_cast<uint8_t>(lcg_.next() % kMaxJoinBackoff);
            } else {
                joinCountdown_--;
                return nullptr;
            }
        }

        refreshTxAck();
        Packet* txPacket = nextTxPacket_;
        nextTxPacket_ = nullptr;

        stats_.txPackets++;
        if (packetType(txPacket) == PacketType::NodeData) {
            stats_.txDataPackets++;
        }
        if (MICROBUS_LOG_PACKETS && MICROBUS_LOGGING) {
            printPacket(txPacket, false, nodeId, true, 0);
        }

        // We sent to the master, so refresh our own keep-alive timer.
        nodeRecordTxPacketSent(&timeToLive);
        return txPacket;
    }

    // ---- membership -------------------------------------------------------

    void checkIfTimedOut() noexcept {
        if (nodeHasTimedOut(timeToLive)) {
            removeFromNetwork();
        }
    }

    void removeFromNetwork() noexcept {
        if (nodeId != kUnallocatedNode) {
            MB_LOG("Node:%u, left network, 0x%llx\n", nodeId,
                   (unsigned long long)uniqueId_);
            const uint32_t left = stats_.nodeLeftNw;
            const uint32_t joined = stats_.nodeJoinedNw;
            reset();
            stats_.nodeLeftNw = left + 1;
            stats_.nodeJoinedNw = joined;
        }
    }

    // ---- state ------------------------------------------------------------
    bool initialised_ = false;
    uint64_t uniqueId_ = 0;

    RxQueue<MaxRxPacketEntries> rxQueue_{};
    PacketEntry* nextRxEntry_ = nullptr;
    PacketEntry* prevRxEntry_ = nullptr;

    // One destination only: the master. Depth is set by the MaxTxPackets
    // template parameter (defaults to 10; node boards override to their
    // MAX_MICROBUS_TX_PACKETS to save RAM).
    TxEngine<1, MaxTxPackets, false> txEngine_{};
    Packet* nextTxPacket_ = nullptr;

    NodeId currentTxNode_ = kMasterNode;
    std::array<NodeId, kMaxScheduledSlots> nextTxNodeId_{};

    bool sentNewNodeRequest_ = false;
    uint8_t joinCountdown_ = 0;

    bool savedRxAckValid_ = false;
    uint8_t savedRxAck_ = 0;
    bool validRxSeqNum_ = false;
    bool validRxPacket_ = false;

    NodeStats stats_{};
    Lcg lcg_{};

    // Scratch buffers for outgoing empty / join packets.
    uint8_t emptyPacketCycle_ = 0;
    std::array<PacketStub, 2> tmpEmptyPacket_{};
    Packet tmpPacket_{};
};

}  // namespace microbus
