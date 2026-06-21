// Copyright (c) 2025 Sean Bremner
// Licensed under the MIT License. See LICENSE file for details.

#pragma once

#include <cstdint>
#include <array>

#include "microbus/src/config.hpp"
#include "microbus/src/diagnostics.hpp"
#include "microbus/src/master_tx.hpp"
#include "microbus/src/network.hpp"
#include "microbus/src/node_queue.hpp"
#include "microbus/src/packet.hpp"
#include "microbus/src/rx_queue.hpp"
#include "microbus/src/scheduler.hpp"
#include "microbus/src/stats.hpp"

namespace microbus {

// ===========================================================================
//                                 Master
//
// The single coordinator on the bus. Every slot it broadcasts a packet whose
// header carries the schedule (which nodes transmit in the upcoming slots) plus
// acks for what it has received; nodes listen to that schedule to know when to
// speak. The master also:
//
//   - admits new nodes (allocates ids via NetworkManager);
//   - decides the schedule (MasterScheduler);
//   - runs a reliable, in-order tx engine per destination (MasterTxEngine);
//   - reaps nodes that stop responding.
//
// The orchestration that the original split across master/masterRx/masterTx is
// folded together here: the rx and tx "paths" are private methods that share
// the master's own state instead of passing pointers between separate objects.
//
// Like Node, it supports a full-duplex pipelined link and a half-duplex link.
// MaxRxPacketEntries sizes the receive queue at compile time.
// ===========================================================================
template <int MaxRxPacketEntries>
class Master {
public:
    // Exposed for the host test harness.
    NodeId currentTxNode = kMasterNode;

    // ---- full duplex (pipelined), driven by the slot ISR ------------------

    void fullDuplexBeginSlot(Packet** txPacket, Packet** rxBufferOut, bool crcError) noexcept {
        advanceSchedule();
        rxQuickProcessPrev(crcError);
        txQuickUpdatePacket();
        *txPacket = nextTxPacket_;
        *rxBufferOut = &nextRxEntry_->packet;
    }

    // Returns the number of tx packets freed this slot.
    uint8_t fullDuplexEndSlot() noexcept {
        uint8_t numTxFreed = rxProcess();
        txPrepareNext();
        numTxFreed += reapTimedOutNodes();
        return numTxFreed;
    }

    // ---- half duplex, driven by the slot ISR ------------------------------

    Packet* rxBuffer() noexcept {
        return &nextRxEntry_->packet;
    }

    uint8_t halfDuplexReceive(bool crcError) noexcept {
        advanceSchedule();
        rxQuickProcessPrev(crcError);
        return rxProcess();
    }

    uint8_t halfDuplexTransmit(Packet** txPacket) noexcept {
        txPrepareNext();
        advanceSchedule();
        txQuickUpdatePacket();
        *txPacket = nextTxPacket_;
        return reapTimedOutNodes();
    }

    // ---- time (timer thread) ---------------------------------------------

    void updateTimeUs(uint32_t usIncr) noexcept {
        nwManager_.updateTimeUs(nodeTtl_.data(), usIncr);
    }

    // ---- lifecycle (main thread) -----------------------------------------

    void init(uint8_t numTxNodesScheduled) noexcept {
        currentTxNode = kMasterNode;
        nextTxNodeId_.fill(kMasterNode);
        nodeTtl_.fill(0);
        stats_ = NodeStats{};

        activeNodes_.numNodes = 0;
        nodesWithTxData_.numNodes = 0;
        activeTxNodes_.numNodes = 0;

        nwManager_.init(&activeNodes_);
        scheduler_.init(&activeNodes_, &activeTxNodes_, &nodesWithTxData_, numTxNodesScheduled);
        txEngine_.init(&activeTxNodes_);

        // Rx path.
        rxQueue_.init();
        nextRxEntry_ = rxQueue_.findFreePacket();
        MB_ASSERT(nextRxEntry_ != nullptr, "master rx queue empty after init");
        prevRxEntry_ = nullptr;
        validRxPacket_ = false;
        validRxSeqNum_ = false;

        // Tx path: start holding an empty master packet.
        nextTxPacket_ = &tmpPacket_;
        setVersionAndType(nextTxPacket_, PacketType::MasterEmpty);
        nextTxPacket_->dataSize1 = 0;
        nextTxPacket_->dataSize2 = 0;
        nextTxPacket_->master.dstNodeId = kInvalidNode;
        emptyPacketCycle_ = 0;

        // Announce ourselves: broadcast resets so any nodes still holding a
        // stale id drop it and rejoin cleanly.
        resetCyclesRemaining_ = 20;
    }

    // ---- application API --------------------------------------------------

    uint8_t* allocateTxPacket() noexcept {
        return txEngine_.allocateTxPacket(kMasterNode, &stats_);
    }

    void submitTxPacket(NodeId dstNodeId, uint16_t numBytes) noexcept {
        txEngine_.submitAllocatedTxPacket(&nodeTtl_[dstNodeId], kMasterNode, dstNodeId,
                                          PacketType::MasterData, numBytes);
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
        *srcNodeId = packet->node.srcNodeId;
        return packet->node.data;
    }

    bool popRxData() noexcept {
        return rxQueue_.popNextDataPacket();
    }

    // Set a bit per node currently connected (fully joined). Bit 0 is us.
    void getConnectedNodes(uint8_t bitfield[kMaxNodes / 8]) noexcept {
        nodeTtl_[kMasterNode] = 1;  // mark ourselves active
        for (uint32_t node = 0; node < kMaxNodes; ++node) {
            if (nodeTtl_[node] > 0) {
                bitfield[node / 8] |= (0x1 << (7 - (node % 8)));
            }
        }
        // Mid-join nodes haven't fully joined; clear them.
        nwManager_.clearUnjoinedNodes(bitfield);
    }

    // Buffered tx packets to a given node. Used by tests.
    uint8_t txBufferedFor(NodeId nodeId) noexcept {
        return txEngine_.bufferedCount(nodeId);
    }

    // Number of fully-joined nodes (excluding the master). Side-effect free:
    // a plain read of the active-node set, unlike getConnectedNodes(). Used by
    // the SPI driver's "have we heard any node lately?" health check.
    uint8_t numActiveNodes() const noexcept {
        return activeNodes_.numNodes;
    }

private:
    // ---- schedule ---------------------------------------------------------

    void advanceSchedule() noexcept {
        if (resetCyclesRemaining_ > 0) {
            currentTxNode = kMasterNode;
            for (uint8_t i = 1; i < scheduler_.numTxNodesScheduled + 1; ++i) {
                nextTxNodeId_[i] = kMasterNode;
            }
            return;
        }

        currentTxNode = nextTxNodeId_[0];
        const uint8_t txBufferLevel = txEngine_.totalBuffered();
        scheduler_.updateAndCalcNextTxNodes(nextTxNodeId_.data(), txBufferLevel);
    }

    // ---- receive path -----------------------------------------------------

    // Quick pass over the just-received packet: validate, record liveness, and
    // check sequence numbers so we can ack promptly. Defers slower work.
    void rxQuickProcessPrev(bool rxCrcError) noexcept {
        prevRxEntry_ = nextRxEntry_;
        Packet* rxPacket = &prevRxEntry_->packet;
        validRxPacket_ = false;

        if (rxPacket->protocolVersionAndPacketType == 0 ||
            rxPacket->protocolVersionAndPacketType == 255) {
            stats_.emptyRx++;
            return;
        }
        if (rxCrcError) {
            stats_.rxCrcFailures++;
            return;
        }
        if (protocolVersion(rxPacket) != kProtocolVersion) {
            stats_.rxInvalidProtocol++;
            return;
        }
        if (dataSize(rxPacket) >= kMaxDataSize) {
            stats_.rxInvalidDataSize++;
            return;
        }

        stats_.rxValid++;
        validRxPacket_ = true;

        // Note liveness from any node packet before we worry about buffering.
        const PacketType type = packetType(rxPacket);
        if (type == PacketType::NodeData || type == PacketType::NodeEmpty) {
            if (nodeTtl_[rxPacket->node.srcNodeId] > 0) {
                nwManager_.recordRxPacket(nodeTtl_.data(), rxPacket->node.srcNodeId);
            }
        }

        // We will likely store this packet; grab a fresh buffer for next rx.
        PacketEntry* freeEntry = rxQueue_.findFreePacket();
        if (freeEntry) {
            nextRxEntry_ = freeEntry;
        } else {
            stats_.rxBufferFull++;
            validRxPacket_ = false;  // reuse this buffer
            return;
        }

        if (type == PacketType::NodeData) {
            validRxSeqNum_ =
                txEngine_.rxCheckAndAdvanceSeq(rxPacket->node.srcNodeId, rxPacket->txSeqNum);
        }
    }

    // Slower handling of the validated packet: apply acks, store data, admit
    // new nodes. Returns tx packets freed by acks.
    uint8_t rxProcess() noexcept {
        if (!validRxPacket_) {
            return 0;
        }

        uint8_t numTxFreed = 0;
        PacketEntry* rxEntry = prevRxEntry_;
        Packet* rxPacket = &rxEntry->packet;
        const PacketType type = packetType(rxPacket);
        bool packetStored = false;
        stats_.rxPacketEntries++;

        if (MICROBUS_LOG_PACKETS && MICROBUS_LOGGING) {
            printPacket(rxPacket, true, 0, false, scheduler_.numTxNodesScheduled);
        }

        switch (type) {
            case PacketType::NodeEmpty: {
                const NodeId src = rxPacket->node.srcNodeId;
                numTxFreed += txEngine_.applyAck(src, rxPacket->node.ackSeqNum,
                                                 &stats_.txWindowRestarts);
                break;
            }
            case PacketType::NodeData: {
                const NodeId src = rxPacket->node.srcNodeId;
                numTxFreed += txEngine_.applyAck(src, rxPacket->node.ackSeqNum,
                                                 &stats_.txWindowRestarts);
                if (validRxSeqNum_) {
                    MB_ASSERT(dataSize(rxPacket) > 0, "node data packet with zero size");
                    scheduler_.updateNodeTxBufferLevel(src, rxPacket->node.bufferLevel);
                    stats_.rxDataPackets++;
                    rxQueue_.addDataPacket(rxEntry);
                    packetStored = true;
                }
                break;
            }
            case PacketType::NewNodeRequest:
                nwManager_.rxNewNodeRequest(nodeTtl_.data(), rxPacket,
                                            &stats_.networkFullCount);
                stats_.newNodeRequestRx++;
                scheduler_.onNewNodeHeard();
                break;
            default:
                stats_.rxInvalidPacketType++;
                break;
        }

        if (!packetStored) {
            rxEntry->inUse = false;
        }
        validRxPacket_ = false;
        validRxSeqNum_ = false;
        return numTxFreed;
    }

    // ---- transmit path ----------------------------------------------------

    // Stamp the outgoing packet's header with the current schedule and acks.
    void txQuickUpdatePacket() noexcept {
        if (nextTxPacket_ == nullptr) {
            return;
        }
        for (uint8_t i = 0; i < scheduler_.numTxNodesScheduled; ++i) {
            const NodeId nodeId = nextTxNodeId_[i];  // one-slot lookahead
            nextTxPacket_->master.nextTxNodeId[i] = nodeId;
            nextTxPacket_->master.nextTxNodeAckSeqNum[i] = txEngine_.expectedRxSeq(nodeId);
        }
        // Mark the remaining schedule slots unused. Without this, a node scanning
        // all kMaxScheduledSlots entries could match its own id in a stale slot
        // left over in the packet buffer and act on a bogus ack.
        for (uint8_t i = scheduler_.numTxNodesScheduled; i < kMaxScheduledSlots; ++i) {
            nextTxPacket_->master.nextTxNodeId[i] = kInvalidNode;
        }
        stats_.txPackets++;
        if (MICROBUS_LOG_PACKETS && MICROBUS_LOGGING) {
            printPacket(nextTxPacket_, true, 0, true, scheduler_.numTxNodesScheduled);
        }
    }

    // Decide the next packet to transmit: reset, new-node response, buffered
    // data, or an empty keep-alive.
    void txPrepareNext() noexcept {
        Packet* txPacket = nullptr;

        if (resetCyclesRemaining_ > 0) {
            resetCyclesRemaining_--;
            txPacket = &tmpPacket_;
            setVersionAndType(txPacket, PacketType::MasterReset);
            MB_LOG("Master reset cycle\n");
        } else {
            // Rotate a small counter; new-node responses go out only on one
            // phase so a join storm can't consume all the bandwidth.
            emptyPacketCycle_ = static_cast<uint8_t>((emptyPacketCycle_ + 1) % 4);

            if (nwManager_.hasPendingNewNodes() && emptyPacketCycle_ == 1) {
                txPacket = &tmpPacket_;
                stats_.newNodeAllocated++;
                nwManager_.txNewNodeResponse(txPacket);
            } else {
                // TODO: known limitation - on a single channel with >1 node and
                // only the master sending data, throughput drops below 50% because
                // we keep alternating data/ack every slot. Sending bursts of up to
                // ~3 to one destination and only scheduling acks every ~4 slots
                // could lift this toward ~75% (hence the burstSize hook below).
                const uint8_t burstSize = 1;
                txPacket = txEngine_.nextTxDataPacket(burstSize);

                if (txPacket == nullptr) {
                    // Always send something on our turn; an empty stub will do.
                    PacketStub* stub = &tmpEmptyPacket_[emptyPacketCycle_ % 2];
                    setVersionAndType(stub, PacketType::MasterEmpty);
                    stub->dataSize1 = 0;
                    stub->dataSize2 = 0;
                    stub->master.dstNodeId = kInvalidNode;
                    txPacket = reinterpret_cast<Packet*>(stub);
                } else {
                    MB_ASSERT(dataSize(txPacket) < kMaxDataSize, "tx packet too large");
                    stats_.txDataPackets++;
                }
            }
        }

        nextTxPacket_ = txPacket;
    }

    // ---- membership reaping ----------------------------------------------

    // Sweep nodes the timer marked for removal (ttl == kRemoveNodeTtl).
    uint8_t reapTimedOutNodes() noexcept {
        uint8_t numTxFreed = 0;
        for (uint32_t nodeId = kFirstNode; nodeId < kMaxNodes; ++nodeId) {
            if (nodeTtl_[nodeId] == kRemoveNodeTtl) {
                nodeTtl_[nodeId] = 0;
                nwManager_.removePending(nodeId);
                activeNodes_.removeIfExists(nodeId);
                scheduler_.resetNodeTxState(nodeId);
                txEngine_.removeNode(nodeId, &numTxFreed);
                rxQueue_.removeAllPackets(nodeId);
                MB_LOG("Master - Node:%u, removed from network\n", nodeId);
            }
        }
        return numTxFreed;
    }

    // ---- state ------------------------------------------------------------
    MasterScheduler scheduler_{};
    NetworkManager nwManager_{};
    // 20 outgoing packets shared across all nodes.
    // TODO: revisit this capacity once real aggregate traffic is known.
    MasterTxEngine<kMaxNodes, 20> txEngine_{};
    NodeStats stats_{};

    // Per-node liveness countdowns (kRemoveNodeTtl marks one for the next sweep).
    std::array<uint8_t, kMaxNodes> nodeTtl_{};
    NodeQueue activeNodes_{};
    NodeQueue nodesWithTxData_{};
    NodeQueue activeTxNodes_{};

    // Schedule lookahead (slot 0 = current). Sized with headroom like the orig.
    std::array<NodeId, kMaxScheduledSlots + 2> nextTxNodeId_{};

    // Rx path state.
    RxQueue<MaxRxPacketEntries> rxQueue_{};
    PacketEntry* nextRxEntry_ = nullptr;
    PacketEntry* prevRxEntry_ = nullptr;
    // These flags carry the result of the quick rx pass over to the later
    // processing pass within the same slot.
    // TODO: not ideal to communicate between calls via member state; a small
    // explicit state machine would read better.
    bool validRxPacket_ = false;
    bool validRxSeqNum_ = false;

    // Tx path state.
    Packet* nextTxPacket_ = nullptr;
    uint32_t resetCyclesRemaining_ = 0;
    uint8_t emptyPacketCycle_ = 0;
    std::array<PacketStub, 2> tmpEmptyPacket_{};
    Packet tmpPacket_{};
};

}  // namespace microbus
