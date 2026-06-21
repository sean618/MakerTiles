// Copyright (c) 2025 Sean Bremner
// Licensed under the MIT License. See LICENSE file for details.

#include "microbus/test/test_support.hpp"

#include <cassert>
#include <cstdlib>
#include <cstring>

namespace microbus::test {

static const Packet kNullPacket{};

void* testMalloc(std::size_t numBytes) noexcept {
    void* result = std::malloc(numBytes);
    assert(result);
    return result;
}

static constexpr uint8_t kSingleChannelScheduledSlots = 4;

TestMaster* createMaster(bool singleChannel) noexcept {
    auto* master = static_cast<TestMaster*>(testMalloc(sizeof(TestMaster)));
    const auto scheduledSlots =
        static_cast<uint8_t>(singleChannel ? kSingleChannelScheduledSlots : 1);
    master->init(scheduledSlots);
    return master;
}

TestNode* createNode(uint64_t uniqueId) noexcept {
    auto* node = static_cast<TestNode*>(testMalloc(sizeof(TestNode)));
    if (uniqueId == 0) {
        uniqueId = (static_cast<uint64_t>(std::rand()) << 32) |
                   static_cast<uint64_t>(std::rand());
    }
    node->init(uniqueId);
    return node;
}

void freeNode(TestNode* node) noexcept { std::free(node); }
void freeMaster(TestMaster* master) noexcept { std::free(master); }

void initSystem(PacketChecker* checker, TestMaster** master, TestNode* nodes[kMaxNodes],
                uint32_t numNodes, bool singleChannel) noexcept {
    cycleIndex = 0;
    initPacketChecker(checker);
    *master = createMaster(singleChannel);
    for (uint32_t i = 1; i < numNodes + 1; ++i) {
        nodes[i] = createNode(0);
    }
}

// ---------------------------------------------------------------------------

static void checkAllNodesHaveDistinctIds(uint32_t i, TestNode* nodes[],
                                         uint32_t numNodes) noexcept {
    for (uint32_t n = 1; n < numNodes; ++n) {
        if (n != i && nodes[n]->nodeId != kUnallocatedNode) {
            if (nodes[i]->nodeId == nodes[n]->nodeId) {
                if (nodes[i]->timeToLive > 0 && nodes[n]->timeToLive > 0) {
                    MB_ASSERT(nodes[i]->nodeId != nodes[n]->nodeId, "duplicate node id");
                }
            }
        }
    }
}

// Half-duplex: each slot is either a master tx or a node tx, never both.
static void runSingleChannel(TestMaster* master, TestNode* nodes[], bool ignoreNodes[],
                             uint32_t numNodes, uint32_t numFrames,
                             bool allowNodeTxOverlaps) noexcept {
    for (uint32_t j = 0; j < numFrames; ++j) {
        (void)j;
        cycleIndex++;

        Packet* nodeTxData = nullptr;
        uint32_t numNodeTxPackets = 0;
        Packet* masterTxPacket = nullptr;
        const bool masterTx = (master->currentTxNode == kMasterNode);

        master->updateTimeUs(kSlotTimeUs);

        if (masterTx) {
            master->halfDuplexTransmit(&masterTxPacket);
            if (masterTxPacket != nullptr) {
                numNodeTxPackets++;
            }
        }

        for (uint32_t i = 0; i < numNodes; ++i) {
            TestNode* node = nodes[i];
            Packet* nodeTxPacket = nullptr;

            node->updateTimeUs(kSlotTimeUs);

            if (ignoreNodes == nullptr || !ignoreNodes[i]) {
                const bool nodeTx = node->isTxSlot();
                if (nodeTx) {
                    node->halfDuplexTransmit(&nodeTxPacket);
                    if (nodeTxPacket != nullptr) {
                        if (masterTxPacket != nullptr) {
                            assert(0 && "single-channel master/node tx overlap");
                        }
                        numNodeTxPackets++;
                        nodeTxData = nodeTxPacket;
                        if (numNodeTxPackets > 1) {
                            if (allowNodeTxOverlaps) {
                                nodeTxData = nullptr;
                            } else {
                                assert(0 && "node tx overlap");
                            }
                        }
                    }
                } else {
                    Packet* nodeRxPacket = node->rxBuffer();
                    std::memcpy(nodeRxPacket, masterTxPacket ? masterTxPacket : &kNullPacket,
                                sizeof(Packet));
                    node->halfDuplexReceive(false);
                }
            }
            checkAllNodesHaveDistinctIds(i, nodes, numNodes);
        }

        if (!masterTx) {
            Packet* masterRxPacket = master->rxBuffer();
            std::memcpy(masterRxPacket, nodeTxData ? nodeTxData : &kNullPacket, sizeof(Packet));
            master->halfDuplexReceive(false);
        }
    }
}

// Full-duplex pipelined: master and one scheduled node exchange a packet each
// slot. endSlot() processes the previous exchange, beginSlot() sets up the next.
static void runDualChannel(TestMaster* master, TestNode* nodes[], bool ignoreNodes[],
                           uint32_t numNodes, uint32_t numFrames,
                           bool allowNodeTxOverlaps) noexcept {
    for (uint32_t j = 0; j < numFrames; ++j) {
        (void)j;
        cycleIndex++;

        Packet* nodeTxData = nullptr;
        uint32_t numNodeTxPackets = 0;

        Packet* masterRxPacket = nullptr;
        Packet* masterTxPacket = nullptr;
        master->updateTimeUs(kSlotTimeUs);
        master->fullDuplexEndSlot();
        master->fullDuplexBeginSlot(&masterTxPacket, &masterRxPacket, false);

        for (uint32_t i = 0; i < numNodes; ++i) {
            TestNode* node = nodes[i];
            Packet* nodeTxPacket = nullptr;
            Packet* nodeRxPacket = nullptr;

            node->updateTimeUs(kSlotTimeUs);

            if (ignoreNodes == nullptr || !ignoreNodes[i]) {
                node->fullDuplexEndSlot();
                node->fullDuplexBeginSlot(&nodeTxPacket, &nodeRxPacket, false);

                if (nodeTxPacket != nullptr) {
                    numNodeTxPackets++;
                    nodeTxData = nodeTxPacket;
                    if (numNodeTxPackets > 1) {
                        if (allowNodeTxOverlaps) {
                            nodeTxData = nullptr;
                        } else {
                            assert(0 && "node tx overlap");
                        }
                    }
                }

                std::memcpy(nodeRxPacket, masterTxPacket ? masterTxPacket : &kNullPacket,
                            sizeof(Packet));
            }
            checkAllNodesHaveDistinctIds(i, nodes, numNodes);
        }

        std::memcpy(masterRxPacket, nodeTxData ? nodeTxData : &kNullPacket, sizeof(Packet));
    }
}

void run(TestMaster* master, TestNode* nodes[], bool ignoreNodes[], uint32_t numNodes,
         uint32_t numFrames, bool allowNodeTxOverlaps, bool singleChannel) noexcept {
    if (singleChannel) {
        runSingleChannel(master, nodes, ignoreNodes, numNodes, numFrames, allowNodeTxOverlaps);
    } else {
        runDualChannel(master, nodes, ignoreNodes, numNodes, numFrames, allowNodeTxOverlaps);
    }
}

void runUntilAllNodesOnNetwork(TestMaster** master, TestNode* nodes[kMaxNodes],
                               uint32_t numNodes, bool disableLogging,
                               bool singleChannel) noexcept {
    if (disableLogging) {
        loggingEnabled = false;
    }
    for (uint32_t j = 0; j < 300 * numNodes; ++j) {
        (void)j;
        run(*master, &nodes[1], nullptr, numNodes, 1, true, singleChannel);
        bool allAllocated = true;
        for (uint32_t i = 1; i < numNodes + 1; ++i) {
            if (nodes[i]->nodeId == kUnallocatedNode || nodes[i]->timeToLive <= 0) {
                allAllocated = false;
            }
        }
        if (allAllocated) {
            break;
        }
    }
    for (uint32_t i = 1; i < numNodes + 1; ++i) {
        assert(nodes[i]->nodeId != kUnallocatedNode);
        assert(nodes[i]->timeToLive > 0);
    }
    if (disableLogging) {
        loggingEnabled = true;
    }
}

// ---------------------------------------------------------------------------

bool attemptMasterTxRandomPacket(PacketChecker* checker, TestMaster* master, TestNode* nodes[],
                                 NodeId dstSimNodeId, bool mightBeDropped) noexcept {
    uint8_t* masterTxData = master->allocateTxPacket();
    if (masterTxData == nullptr) {
        return false;
    }
    const auto size = static_cast<uint16_t>(
        kCheckerHeader + std::rand() % (kMaxDataSize - kCheckerHeader));
    uint8_t* data = createTxPacket(checker, 0, dstSimNodeId, size, mightBeDropped);
    std::memcpy(masterTxData, data, size);
    master->submitTxPacket(nodes[dstSimNodeId]->nodeId, size);
    return true;
}

bool attemptNodeTxRandomPacket(PacketChecker* checker, TestNode* node, NodeId srcSimNodeId,
                               bool mightBeDropped) noexcept {
    uint8_t* nodeTxData = node->allocateTxPacket();
    if (nodeTxData == nullptr) {
        return false;
    }
    const auto size = static_cast<uint16_t>(
        kCheckerHeader + std::rand() % (kMaxDataSize - kCheckerHeader));
    uint8_t* data = createTxPacket(checker,  srcSimNodeId, 0, size, mightBeDropped);
    std::memcpy(nodeTxData, data, size);
    node->submitTxPacket(0, size);
    return true;
}

void fillTxBuffersWithRandomPackets(uint32_t numPackets, PacketChecker* checker,
                                    TestMaster* master, TestNode* nodes[kMaxNodes],
                                    uint32_t numNodes, uint32_t packetsSent[kMaxNodes],
                                    uint32_t* numPacketsSent, bool onlyMaster,
                                    bool onlyFirstNode, bool mightBeDropped) noexcept {
    if (!onlyFirstNode) {
        while (*numPacketsSent < numPackets) {
            const auto dstSimNodeId = static_cast<NodeId>(1 + (std::rand() % numNodes));
            if (attemptMasterTxRandomPacket(checker, master, nodes, dstSimNodeId, mightBeDropped)) {
                (*numPacketsSent)++;
                packetsSent[0]++;
            } else {
                break;
            }
        }
    }

    if (!onlyMaster) {
        for (uint32_t i = 1; i < numNodes + 1; ++i) {
            if (i == 1 || !onlyFirstNode) {
                while (*numPacketsSent < numPackets) {
                    if (attemptNodeTxRandomPacket(checker, nodes[i], static_cast<NodeId>(i),
                                                  mightBeDropped)) {
                        (*numPacketsSent)++;
                        packetsSent[i]++;
                    } else {
                        break;
                    }
                }
            }
        }
    }
}

bool areAllTxBuffersEmpty(TestMaster* master, TestNode* nodes[kMaxNodes], uint32_t numNodes,
                          bool printFalse) noexcept {
    (void)printFalse;
    for (uint32_t i = 1; i < numNodes + 1; ++i) {
        if (nodes[i]->timeToLive > 0) {
            if (master->txBufferedFor(nodes[i]->nodeId) > 0) {
                MB_LOG("Master -> Node:%u, ttl:%d, buffered:%u\n", nodes[i]->nodeId,
                       nodes[i]->timeToLive, master->txBufferedFor(nodes[i]->nodeId));
                return false;
            }
            if (nodes[i]->txBuffered() > 0) {
                MB_LOG("Node:%u -> Master, ttl:%d, buffered:%u\n", nodes[i]->nodeId,
                       nodes[i]->timeToLive, nodes[i]->txBuffered());
                return false;
            }
        }
    }
    return true;
}

// ---------------------------------------------------------------------------

void getMasterRxData(TestMaster* master, uint8_t* rxData, uint8_t numBytes) noexcept {
    uint16_t size = 0;
    NodeId srcNodeId = 0;
    uint8_t* data = master->peekRxData(&size, &srcNodeId);
    assert(data);
    std::memcpy(rxData, data, numBytes);
    master->popRxData();
}

void getNodeRxData(TestNode* node, uint8_t* rxData, uint8_t numBytes) noexcept {
    uint16_t size = 0;
    NodeId srcNodeId = 0;
    uint8_t* data = node->peekRxData(&size, &srcNodeId);
    std::memcpy(rxData, data, numBytes);
    node->popRxData();
}

uint32_t processAllRxData(PacketChecker* checker, TestMaster* master,
                               TestNode* nodes[kMaxNodes], uint32_t numNodes) noexcept {
    uint32_t numReceived = 0;
    while (true) {
        uint16_t size = 0;
        NodeId srcNodeId = 0;
        uint8_t* masterRxData = master->peekRxData(&size, &srcNodeId);
        if (masterRxData) {
            processRxPacket(checker, masterRxData, size);
            master->popRxData();
            numReceived++;
        } else {
            break;
        }
    }
    for (uint32_t i = 1; i < numNodes + 1; ++i) {
        while (true) {
            uint16_t size = 0;
            NodeId srcNodeId = 0;
            uint8_t* nodeRxData = nodes[i]->peekRxData(&size, &srcNodeId);
            if (nodeRxData) {
                processRxPacket(checker, nodeRxData, size);
                nodes[i]->popRxData();
                numReceived++;
            } else {
                break;
            }
        }
    }
    return numReceived;
}

}  // namespace microbus::test
