// Copyright (c) 2025 Sean Bremner
// Licensed under the MIT License. See LICENSE file for details.
//
// Basic end-to-end tests: nodes join, then a known packet is sent in each
// direction for every node and checked on arrival.

#include <array>
#include <cassert>
#include <cstdint>

#include "microbus/src/microbus.hpp"
#include "microbus/test/test_support.hpp"

namespace microbus::test {

void test_max_nodes_new_node_allocation() noexcept {
    constexpr uint32_t kNumNodes = kMaxNodes - 1;

    for (int z = 0; z < 10; ++z) {
        TestMaster* master = createMaster(false);
        std::array<TestNode*, kNumNodes> nodes{};
        for (uint32_t i = 0; i < kNumNodes; ++i) {
            nodes[i] = createNode(0);
        }

        run(master, nodes.data(), nullptr, kNumNodes, 4000, true, false);

        for (uint32_t i = 0; i < kNumNodes; ++i) {
            assert(nodes[i]->nodeId != kUnallocatedNode);
            freeNode(nodes[i]);
        }
        freeMaster(master);
    }
}

void test_packets_to_from_each_node(uint32_t numNodes, uint32_t numPacketsPerNode) noexcept {
    TestMaster* master = createMaster(false);
    std::array<TestNode*, kMaxNodes> nodes{};
    for (uint32_t i = 0; i < numNodes; ++i) {
        nodes[i] = createNode(0);
    }

    run(master, nodes.data(), nullptr, numNodes, 4000, true, false);

    for (uint32_t i = 0; i < numNodes; ++i) {
        assert(nodes[i]->nodeId != kUnallocatedNode);
    }

    for (uint32_t i = 0; i < numNodes; ++i) {
        for (uint32_t z = 0; z < numPacketsPerNode; ++z) {
            uint8_t* masterTxData = master->allocateTxPacket();
            assert(masterTxData);
            masterTxData[0] = 0xAB;
            masterTxData[1] = static_cast<uint8_t>(i);
            masterTxData[2] = static_cast<uint8_t>(z);
            master->submitTxPacket(nodes[i]->nodeId, 3);

            uint8_t* nodeTxData = nodes[i]->allocateTxPacket();
            assert(nodeTxData);
            nodeTxData[0] = 0xCD;
            nodeTxData[1] = static_cast<uint8_t>(i);
            nodeTxData[2] = static_cast<uint8_t>(z);
            nodes[i]->submitTxPacket(0, 3);
        }
    }

    run(master, nodes.data(), nullptr, numNodes, 1000, false, false);

    std::array<bool, kMaxNodes> masterPacketReceived{};
    for (uint32_t i = 0; i < numNodes * numPacketsPerNode; ++i) {
        std::array<uint8_t, 3> masterRxData{};
        getMasterRxData(master, masterRxData.data(), 3);
        assert(masterRxData[0] == 0xCD);
        masterPacketReceived[masterRxData[1]] = true;
    }
    for (uint32_t i = 0; i < numNodes; ++i) {
        for (uint32_t z = 0; z < numPacketsPerNode; ++z) {
            std::array<uint8_t, 3> nodeRxData{};
            getNodeRxData(nodes[i], nodeRxData.data(), 3);
            assert(nodeRxData[0] == 0xAB);
            assert(nodeRxData[1] == i);
            assert(nodeRxData[2] == z);
            assert(masterPacketReceived[i] == true);
        }
        assert(0 == nodes[i]->txBuffered());
        assert(0 == master->txBufferedFor(nodes[i]->nodeId));
    }

    for (uint32_t i = 0; i < numNodes; ++i) {
        freeNode(nodes[i]);
    }
    freeMaster(master);
}

}  // namespace microbus::test
