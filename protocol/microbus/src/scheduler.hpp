// Copyright (c) 2025 Sean Bremner
// Licensed under the MIT License. See LICENSE file for details.
//
// scheduler.hpp - the master decides who transmits in each upcoming slot.
//
// Every master packet publishes the next kMaxScheduledSlots slot owners, giving
// each receiver advance notice of when to drive the bus. This scheduler fills
// that look-ahead, balancing four competing demands and rotating fairly:
//
//   * MasterTx   - the master has data to send (dual-channel only)
//   * MasterRxAck- a node we sent data to needs a slot to ack
//   * NodeTx     - a node has data buffered and needs a slot to send it
//   * Service    - a quiet node still needs an occasional slot to prove it's
//                  alive (and to keep its TTL fresh)
//
// Interleaved with the above are periodic "join" (unallocated) slots that let
// new nodes request an id. The join gap starts small for fast enrolment and
// widens once the bus goes quiet so it costs little in steady state.

#pragma once

#include <array>
#include <cstdint>
#include <cstring>

#include "microbus/src/config.hpp"
#include "microbus/src/diagnostics.hpp"
#include "microbus/src/node_queue.hpp"
#include "microbus/src/packet.hpp"

namespace microbus {

enum class SchedulerTurn : uint8_t {
    MasterTx = 0,
    MasterRxAck,
    NodeTx,
    Count,
};

#if MICROBUS_LOG_SCHEDULER > 0
inline const char* turnName(SchedulerTurn t) noexcept {
#else
[[maybe_unused]] inline const char* turnName(SchedulerTurn t) noexcept {
#endif
    switch (t) {
        case SchedulerTurn::MasterTx:    return "MASTER_TX";
        case SchedulerTurn::MasterRxAck: return "MASTER_RX_ACK";
        case SchedulerTurn::NodeTx:      return "NODE_TX";
        default:                         return "?";
    }
}


class MasterScheduler {
public:
    // How many future slots are published per packet. Public: read by the tx
    // path and the packet printer.
    uint8_t numTxNodesScheduled = 0;

    // Current join-slot cadence, public so the rx path can tighten it via
    // onNewNodeHeard() when a join request arrives.
    uint8_t joinSlotGap = 0;
    uint8_t countTillNextJoin = 0;

    void init(NodeQueue* activeNodes, NodeQueue* activeTxNodes,
              NodeQueue* nodesWithTxData, uint8_t scheduledSlots) noexcept {
        activeNodes_ = activeNodes;
        activeTxNodes_ = activeTxNodes;
        nodesWithTxData_ = nodesWithTxData;
        numTxNodesScheduled = scheduledSlots;

        joinGapStableCount_ = 0;
        countTillNextService_ = 0;
        nextTurn_ = SchedulerTurn::MasterTx;
        recentScheduled_.fill(0);
        nodeTxBufferLevel_.fill(0);

        onNewNodeHeard();
    }

    // A join request was heard: pull join slots back to minimum cadence so we
    // enrol new nodes quickly.
    void onNewNodeHeard() noexcept {
        joinSlotGap = kMinSlotsBetweenJoin;
        countTillNextJoin = kMinSlotsBetweenJoin;
    }

    // Advance the published schedule by one slot, appending the newest owner.
    void updateAndCalcNextTxNodes(NodeId schedule[kMaxScheduledSlots],
                                  uint8_t masterTxBufferLevel) noexcept {
        if (numTxNodesScheduled == 1) {
            schedule[0] = scheduleNextNode(0);
            return;
        }

        // Shift the look-ahead down, noting whether the master is already in it.
        bool masterAlreadyScheduled = false;
        for (uint8_t i = 0; i + 1 < numTxNodesScheduled; ++i) {
            schedule[i] = schedule[i + 1];
            if (schedule[i] == kMasterNode) {
                if (masterTxBufferLevel > 0) {
                    --masterTxBufferLevel;
                }
                masterAlreadyScheduled = true;
            }
        }

        // Fill the freed final slot. If the master isn't already due, force it
        // in so the next schedule actually gets transmitted.
        if (masterAlreadyScheduled) {
            schedule[numTxNodesScheduled - 1] = scheduleNextNode(masterTxBufferLevel);
        } else {
            schedule[numTxNodesScheduled - 1] = kMasterNode;
        }
    }

    // Track whether a node still has data to send (drives NodeTx scheduling).
    void updateNodeTxBufferLevel(NodeId src, uint8_t bufferLevel) noexcept {
        if (nodeTxBufferLevel_[src] == 0 && bufferLevel > 0) {
            nodesWithTxData_->add(src);  // 0 -> >0: now wants to send
        } else if (nodeTxBufferLevel_[src] > 0 && bufferLevel == 0) {
            nodesWithTxData_->remove(src);  // >0 -> 0: nothing left
        }
        nodeTxBufferLevel_[src] = bufferLevel;
    }

    // Clear a node's tracked tx state when it leaves, so a stale non-zero level
    // doesn't hide the 0 -> >0 transition (and leave it forever unscheduled)
    // when it later rejoins.
    void resetNodeTxState(NodeId nodeId) noexcept {
        nodesWithTxData_->removeIfExists(nodeId);
        nodeTxBufferLevel_[nodeId] = 0;
    }

private:
    bool wasRecentlyScheduled(NodeId node) const noexcept {
        for (uint8_t i = 0; i + 1 < kMaxSlotsBetweenAcks; ++i) {
            if (node == recentScheduled_[i]) {
                return true;
            }
        }
        return false;
    }

    // Next node owed an ack slot (skipping ones scheduled very recently).
    NodeId nextAckNode() noexcept {
        if (activeTxNodes_ != nullptr) {
            for (uint8_t i = 0; i < activeTxNodes_->numNodes; ++i) {
                const NodeId nodeId = activeTxNodes_->getNextNodeAndIncr();
                if (!wasRecentlyScheduled(nodeId)) {
                    return nodeId;
                }
                MB_ASSERT(i < kMaxSlotsBetweenAcks, "ack scan ran too long");
            }
        }
        return kInvalidNode;
    }

    NodeId nextNodeTxNode() noexcept {
        if (nodesWithTxData_->numNodes > 0) {
            return nodesWithTxData_->getNextNodeAndIncr();
        }
        return kInvalidNode;
    }

    // Next node to "service" (give a slot just to keep it alive). With no nodes
    // yet, returns kUnallocatedNode to offer a join slot.
    NodeId nextServiceNode(bool avoidRecentlyScheduled) noexcept {
        countTillNextService_ = kMaxSlotsBetweenServicing;
        if (activeNodes_->numNodes > 0) {
            const NodeId nodeId = activeNodes_->getNextNodeAndIncr();
            if (!avoidRecentlyScheduled || !wasRecentlyScheduled(nodeId)) {
                MB_LOG_SCHEDULER("Master scheduling SERVICE_NODE, node:%u\n", nodeId);
                return nodeId;
            }
        }
        return kInvalidNode;
    }

    // Pick the owner of a normal (non-join) slot.
    NodeId scheduleAllocatedNode(uint8_t masterTxBufferLevel) noexcept {
        if (countTillNextService_ == 0) {
            const NodeId node = nextServiceNode(true);
            if (node != kInvalidNode) {
                return node;
            }
        }

        // Rotate through the turn types, giving each a chance to claim the slot.
        for (uint8_t i = 0; i < static_cast<uint8_t>(SchedulerTurn::Count); ++i) {
            (void)i;
            NodeId node = kInvalidNode;
            switch (nextTurn_) {
                case SchedulerTurn::MasterTx:
                    if (numTxNodesScheduled > 1 && masterTxBufferLevel > 0) {
                        node = kMasterNode;
                        --masterTxBufferLevel;
                    }
                    break;
                case SchedulerTurn::MasterRxAck:
                    node = nextAckNode();
                    break;
                case SchedulerTurn::NodeTx:
                    node = nextNodeTxNode();
                    break;
                default:
                    MB_ASSERT(false, "bad scheduler turn");
            }

#if MICROBUS_LOG_SCHEDULER > 0
            const SchedulerTurn turn = nextTurn_;
#else
            [[maybe_unused]] const SchedulerTurn turn = nextTurn_;
#endif
            nextTurn_ = static_cast<SchedulerTurn>(
                (static_cast<uint8_t>(nextTurn_) + 1) %
                static_cast<uint8_t>(SchedulerTurn::Count));

            if (node != kInvalidNode) {
                MB_LOG_SCHEDULER("Master scheduling %s, node:%u\n", turnName(turn), node);
                return node;
            }
        }

        // Nobody needed it for tx/ack - use it to service a quiet node.
        return nextServiceNode(false);
    }

    // Decide the owner of the next slot, inserting periodic join slots.
    NodeId scheduleNextNode(uint8_t masterTxBufferLevel) noexcept {
        NodeId node;
        if (countTillNextJoin == 0) {
            MB_LOG_SCHEDULER("Master scheduling ALLOCATION\n");
            node = kUnallocatedNode;

            // If no new node has been heard recently, gradually widen the gap so
            // join slots cost little in steady state; onNewNodeHeard() resets it.
            if (joinSlotGap != kMaxSlotsBetweenJoin) {
                if (++joinGapStableCount_ == kSlotsBeforeJoinGapDoubles) {
                    joinGapStableCount_ = 0;
                    joinSlotGap = static_cast<uint8_t>(joinSlotGap * 2);
                    if (joinSlotGap > kMaxSlotsBetweenJoin) {
                        joinSlotGap = kMaxSlotsBetweenJoin;
                    }
                    MB_LOG_SCHEDULER("Master widening join gap: %u\n", joinSlotGap);
                }
            }
            countTillNextJoin = static_cast<uint8_t>(joinSlotGap - 1);
        } else {
            node = scheduleAllocatedNode(masterTxBufferLevel);
            MB_ASSERT(countTillNextJoin > 0, "join countdown underflow");
            MB_ASSERT(countTillNextService_ > 0, "service countdown underflow");
            --countTillNextJoin;
            --countTillNextService_;
            for (uint8_t i = 0; i + 1 < kMaxSlotsBetweenAcks; ++i) {
                recentScheduled_[i] = recentScheduled_[i + 1];
            }
            recentScheduled_[kMaxSlotsBetweenAcks - 1] = node;
        }
        if (node == kInvalidNode) {
            node = kUnallocatedNode;
        }
        return node;
    }

    uint8_t joinGapStableCount_ = 0;
    uint8_t countTillNextService_ = 0;
    SchedulerTurn nextTurn_ = SchedulerTurn::MasterTx;
    std::array<NodeId, kMaxSlotsBetweenAcks> recentScheduled_{};
    std::array<uint8_t, kMaxNodes> nodeTxBufferLevel_{};
    NodeQueue* activeNodes_ = nullptr;       // every connected node
    NodeQueue* activeTxNodes_ = nullptr;     // nodes we sent data to, awaiting ack
    NodeQueue* nodesWithTxData_ = nullptr;   // nodes with packets buffered to send
};

}  // namespace microbus
