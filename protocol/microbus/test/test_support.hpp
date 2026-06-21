// Copyright (c) 2025 Sean Bremner
// Licensed under the MIT License. See LICENSE file for details.
//
// Host-side simulation harness. It instantiates a Master and some Nodes, then
// "runs" the bus by copying each transmitted Packet into the receiver's rx
// buffer slot by slot - once for a half-duplex (single channel) link and once
// for a full-duplex (dual channel) pipelined link. The PacketChecker verifies
// every payload arrives intact.

#pragma once

#include <array>
#include <cstdint>

#include "microbus/src/microbus.hpp"
#include "microbus/test/packet_checker.hpp"

namespace microbus::test {

// Rx queue sizes are compile-time; pick sizes ample for every scenario here.
inline constexpr int kTestMasterRxEntries = 16;
inline constexpr int kTestNodeRxEntries = 8;
using TestMaster = Master<kTestMasterRxEntries>;
using TestNode = Node<kTestNodeRxEntries>;

void* testMalloc(std::size_t numBytes) noexcept;
TestMaster* createMaster(bool singleChannel) noexcept;
TestNode* createNode(uint64_t uniqueId) noexcept;
void freeNode(TestNode* node) noexcept;
void freeMaster(TestMaster* master) noexcept;
void initSystem(PacketChecker* checker, TestMaster** master, TestNode* nodes[kMaxNodes],
                uint32_t numNodes, bool singleChannel) noexcept;

void run(TestMaster* master, TestNode* nodes[], bool ignoreNodes[], uint32_t numNodes,
         uint32_t numFrames, bool allowNodeTxOverlaps, bool singleChannel) noexcept;
void runUntilAllNodesOnNetwork(TestMaster** master, TestNode* nodes[kMaxNodes],
                               uint32_t numNodes, bool disableLogging,
                               bool singleChannel) noexcept;

bool attemptMasterTxRandomPacket(PacketChecker* checker, TestMaster* master, TestNode* nodes[],
                                 NodeId dstSimNodeId, bool mightBeDropped) noexcept;
bool attemptNodeTxRandomPacket(PacketChecker* checker, TestNode* node, NodeId srcSimNodeId,
                               bool mightBeDropped) noexcept;
void fillTxBuffersWithRandomPackets(uint32_t numPackets, PacketChecker* checker,
                                    TestMaster* master, TestNode* nodes[kMaxNodes],
                                    uint32_t numNodes, uint32_t packetsSent[kMaxNodes],
                                    uint32_t* numPacketsSent, bool onlyMaster,
                                    bool onlyFirstNode, bool mightBeDropped) noexcept;
bool areAllTxBuffersEmpty(TestMaster* master, TestNode* nodes[kMaxNodes], uint32_t numNodes,
                          bool printFalse) noexcept;
void getMasterRxData(TestMaster* master, uint8_t* rxData, uint8_t numBytes) noexcept;
void getNodeRxData(TestNode* node, uint8_t* rxData, uint8_t numBytes) noexcept;
uint32_t processAllRxData(PacketChecker* checker, TestMaster* master,
                               TestNode* nodes[kMaxNodes], uint32_t numNodes) noexcept;

}  // namespace microbus::test
