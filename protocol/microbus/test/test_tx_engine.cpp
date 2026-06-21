// Copyright (c) 2025 Sean Bremner
// Licensed under the MIT License. See LICENSE file for details.
//
// MasterTxEngine tests. These pin the exact sliding-window behaviour: the
// numeric assertions (numFreed == 37 / 25, etc.) only hold if the windowing,
// ack handling, and window-restart logic match the reference implementation.

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdlib>

#include "microbus/src/microbus.hpp"

namespace microbus::test {

static uint32_t myRand(uint32_t max) noexcept {
    if (max == 0) {
        return 0;
    }
    return static_cast<uint32_t>(std::rand()) % max;
}

using TestTxEngine = MasterTxEngine<kMaxNodes, 100>;
static TestTxEngine manager;
static uint64_t txWindowRestarts = 0;
static uint8_t ttl = 1;

static void basicInit(NodeQueue& activeTxNodes) noexcept {
    manager = TestTxEngine{};  // reset all state between tests
    manager.init(&activeTxNodes);
}

static void createMasterTxPacket(TestTxEngine* txEngine, NodeId dstNodeId, bool allowFull) noexcept {
    NodeStats stats{};
    uint8_t* packet = txEngine->allocateTxPacket(kMasterNode, &stats);
    if (allowFull && packet == nullptr) {
        return;
    }
    txEngine->submitAllocatedTxPacket(&ttl, kMasterNode, dstNodeId, PacketType::MasterData, 1);
}

static void test_simple() noexcept {
    // Perfect transmission to one node.
    NodeQueue activeTxNodes{};
    basicInit(activeTxNodes);

    const NodeId dstNodeId = 1;
    for (int i = 0; i < 100 / 2; ++i) {
        createMasterTxPacket(&manager, dstNodeId, false);
    }
    for (int i = 0; i < 100 / 2; ++i) {
        Packet* packet = manager.nextTxDataPacket(1);
        assert(packet != nullptr);
        manager.applyAck(dstNodeId, packet->txSeqNum, &txWindowRestarts);
    }
    assert(manager.numStored() == manager.numFreed());
}

static void test_continuous() noexcept {
    NodeQueue activeTxNodes{};
    basicInit(activeTxNodes);
    const NodeId dstNodeId = 0;
    for (int i = 0; i < 100 / 2; ++i) {
        createMasterTxPacket(&manager, dstNodeId, false);
        Packet* packet = manager.nextTxDataPacket(1);
        assert(packet != nullptr);
        manager.applyAck(dstNodeId, packet->txSeqNum, &txWindowRestarts);
    }
    assert(manager.numStored() == manager.numFreed());
}

static void test_multiple_nodes() noexcept {
    NodeQueue activeTxNodes{};
    basicInit(activeTxNodes);
    for (int i = 0; i < 100 / 2; ++i) {
        createMasterTxPacket(&manager, static_cast<NodeId>(i % 5), false);
    }
    for (int i = 0; i < 100 / 2; ++i) {
        Packet* packet = manager.nextTxDataPacket(1);
        assert(packet != nullptr);
        manager.applyAck(packet->master.dstNodeId, packet->txSeqNum, &txWindowRestarts);
    }
    assert(manager.numStored() == manager.numFreed());
}

static void test_simple_with_dropped_packets() noexcept {
    NodeQueue activeTxNodes{};
    basicInit(activeTxNodes);

    const NodeId dstNodeId = 0;
    for (int i = 0; i < 100 / 2; ++i) {
        createMasterTxPacket(&manager, dstNodeId, false);
    }

    uint8_t lastAckSeqNum = 0;
    for (int i = 0; i < 4 * 100; ++i) {
        Packet* packet = manager.nextTxDataPacket(1);
        if (packet != nullptr) {
            if (myRand(10) == 0) {
                // drop: skip tx
            } else if (packet->txSeqNum == lastAckSeqNum + 1) {
                manager.applyAck(dstNodeId, packet->txSeqNum, &txWindowRestarts);
                lastAckSeqNum = packet->txSeqNum;
                if (manager.numStored() == manager.numFreed()) {
                    break;
                }
            }
        } else {
            manager.applyAck(dstNodeId, lastAckSeqNum, &txWindowRestarts);
        }
    }
    assert(manager.numStored() == manager.numFreed());
}

static void test_multiple_nodes_with_dropped_packets() noexcept {
    for (int j = 0; j < 20; ++j) {
        std::array<uint8_t, 5> lastAckSeqNum{};

        NodeQueue activeTxNodes{};
        basicInit(activeTxNodes);

        for (int i = 0; i < 20; ++i) {
            createMasterTxPacket(&manager, static_cast<NodeId>(myRand(5)), false);
        }
        // Mode 0: keep adding packets. Mode 1: just drain them.
        NodeId rxNodeId = 0;
        for (uint8_t mode = 0; mode < 2; ++mode) {
            for (int i = 0; i < 1000; ++i) {
                if (mode == 0 && myRand(10) < 8) {
                    createMasterTxPacket(&manager, static_cast<NodeId>(myRand(5)), true);
                }

                Packet* packet = manager.nextTxDataPacket(1);
                if (packet != nullptr) {
                    if (myRand(10) == 0) {
                        // drop
                    } else if (packet->txSeqNum == lastAckSeqNum[packet->master.dstNodeId] + 1) {
                        lastAckSeqNum[packet->master.dstNodeId] = packet->txSeqNum;
                    }
                }

                rxNodeId++;
                if (rxNodeId == 5) {
                    rxNodeId = 0;
                }
                manager.applyAck(rxNodeId, lastAckSeqNum[rxNodeId], &txWindowRestarts);

                if (mode == 1 && manager.numStored() == manager.numFreed()) {
                    break;
                }
            }
        }
        assert(manager.numStored() == manager.numFreed());
    }
}

static void test_continuous_with_delayed_acks() noexcept {
    NodeQueue activeTxNodes{};
    basicInit(activeTxNodes);
    const NodeId dstNodeId = 1;
    uint8_t lastTxSeqNum = 0;
    for (int i = 0; i < 100 / 2; ++i) {
        createMasterTxPacket(&manager, dstNodeId, false);
        Packet* packet = manager.nextTxDataPacket(1);
        assert(packet != nullptr);
        lastTxSeqNum = std::max(packet->txSeqNum, lastTxSeqNum);

        // Ack just before the sliding window restarts.
        if (i % (kSlidingWindowSize - 1) == 1) {
            manager.applyAck(dstNodeId, lastTxSeqNum - 1, &txWindowRestarts);
        }
    }
    assert(manager.numStored() == manager.numFreed() + 1);
    assert(manager.numStored() == 50);
}

static void test_continuous_with_extra_delayed_acks() noexcept {
    NodeQueue activeTxNodes{};
    basicInit(activeTxNodes);
    const NodeId dstNodeId = 1;
    uint8_t lastTxSeqNum = 0;
    for (int i = 0; i < 100 / 2; ++i) {
        createMasterTxPacket(&manager, dstNodeId, false);
        Packet* packet = manager.nextTxDataPacket(1);
        if (packet) {
            lastTxSeqNum = std::max(packet->txSeqNum, lastTxSeqNum);
        }
        if (i % (kSlidingWindowSize) == 1) {
            manager.applyAck(dstNodeId, lastTxSeqNum - 1, &txWindowRestarts);
        }
    }
    // ~3/4 bandwidth: every 4th packet stalls waiting for an ack.
    assert(manager.numFreed() == 37);
    assert(manager.numStored() == 50);
}

static void test_continuous_with_extra_delayed_acks_2() noexcept {
    NodeQueue activeTxNodes{};
    basicInit(activeTxNodes);
    const NodeId dstNodeId = 1;
    uint8_t lastTxSeqNum = 0;
    for (int i = 0; i < 100 / 2; ++i) {
        createMasterTxPacket(&manager, dstNodeId, false);
        Packet* packet = manager.nextTxDataPacket(1);
        if (packet) {
            lastTxSeqNum = std::max(packet->txSeqNum, lastTxSeqNum);
        }
        if (i % (kSlidingWindowSize + 2) == 1) {
            manager.applyAck(dstNodeId, lastTxSeqNum - 1, &txWindowRestarts);
        }
    }
    // ~3/6 bandwidth.
    assert(manager.numFreed() == 25);
    assert(manager.numStored() == 50);
}

void testTxManager() noexcept {
    test_simple();
    test_continuous();
    test_multiple_nodes();
    test_simple_with_dropped_packets();
    test_multiple_nodes_with_dropped_packets();
    test_continuous_with_delayed_acks();
    test_continuous_with_extra_delayed_acks();
    test_continuous_with_extra_delayed_acks_2();
}

}  // namespace microbus::test
