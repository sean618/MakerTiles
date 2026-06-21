// Copyright (c) 2025 Sean Bremner
// Licensed under the MIT License. See LICENSE file for details.
//
// Full-system integration tests: loaded throughput, rx overflow, master/node
// restarts, and nodes disconnecting/reconnecting. The PacketChecker is the
// oracle - every non-droppable payload must arrive intact.

#include <array>
#include <cassert>
#include <cstdint>
#include <cstdlib>

#include "microbus/src/microbus.hpp"
#include "microbus/test/packet_checker.hpp"
#include "microbus/test/test_support.hpp"

namespace microbus::test {

static bool disableAllocationLogging = false;

static void test_fully_loaded_system(uint32_t numNodes, uint32_t numPackets,
                                     uint32_t numFramesToRun, uint32_t expMasterBw,
                                     uint32_t expTotalNodeBw, bool singleChannel,
                                     bool onlyMaster, bool onlyFirstNode) noexcept {
    MB_LOG("test_fully_loaded_system, numNodes:%u, numPackets:%u, numFramesToRun:%u\n", numNodes,
           numPackets, numFramesToRun);
    PacketChecker checker{};
    TestMaster* master = nullptr;
    std::array<TestNode*, kMaxNodes> nodes{};
    initSystem(&checker, &master, nodes.data(), numNodes, singleChannel);

    runUntilAllNodesOnNetwork(&master, nodes.data(), numNodes, disableAllocationLogging,
                              singleChannel);

    // Let the join allocation settle so its bandwidth cost is negligible.
    loggingEnabled = false;
    run(master, &nodes[1], nullptr, numNodes, kSlotsBeforeJoinGapDoubles * 64 * 2, false,
        singleChannel);

    std::array<uint32_t, kMaxNodes> packetsSent{};
    uint32_t numPacketsSent = 0;
    uint32_t numPacketsReceived = 0;
    uint32_t framesRun = 0;

    for (uint32_t i = 0; i < numFramesToRun; ++i) {
        fillTxBuffersWithRandomPackets(numPackets, &checker, master, nodes.data(), numNodes,
                                       packetsSent.data(), &numPacketsSent, onlyMaster,
                                       onlyFirstNode, false);
        framesRun++;
        run(master, &nodes[1], nullptr, numNodes, 1, false, singleChannel);
        numPacketsReceived += processAllRxData(&checker, master, nodes.data(), numNodes);

        if (numPacketsReceived >= numPackets && numPacketsReceived == numPacketsSent) {
            if (areAllTxBuffersEmpty(master, nodes.data(), numNodes, false)) {
                break;
            }
        }
    }
    checkAllPacketsReceived(&checker);

    const auto masterBw = static_cast<uint32_t>((100 * packetsSent[0]) / framesRun);
    const auto totalNodeBw =
        static_cast<uint32_t>((100 * (numPackets - packetsSent[0])) / framesRun);
    std::printf("Master BW: %u%%\n", masterBw);
    std::printf("Total Node BW: %u%%\n", totalNodeBw);

    assert(masterBw >= expMasterBw);
    assert(totalNodeBw >= expTotalNodeBw);

    for (uint32_t i = 1; i < numNodes + 1; ++i) {
        freeNode(nodes[i]);
    }
    freeMaster(master);
}

static void test_full_system_with_rx_buffer_overflows(uint32_t numNodes,
                                                       uint32_t numPackets,
                                                       uint32_t numFramesToRun,
                                                       bool singleChannel) noexcept {
    std::printf("test_full_system_with_rx_buffer_overflows, numNodes:%u\n", numNodes);
    PacketChecker checker{};
    TestMaster* master = nullptr;
    std::array<TestNode*, kMaxNodes> nodes{};
    initSystem(&checker, &master, nodes.data(), numNodes, singleChannel);
    runUntilAllNodesOnNetwork(&master, nodes.data(), numNodes, disableAllocationLogging,
                              singleChannel);

    std::array<uint32_t, kMaxNodes> packetsSent{};
    uint32_t numPacketsSent = 0;
    uint32_t numPacketsReceived = 0;

    for (uint32_t i = 0; i < numFramesToRun; ++i) {
        fillTxBuffersWithRandomPackets(numPackets, &checker, master, nodes.data(), numNodes,
                                       packetsSent.data(), &numPacketsSent, false, false, false);
        run(master, &nodes[1], nullptr, numNodes, 10, false, singleChannel);
        numPacketsReceived += processAllRxData(&checker, master, nodes.data(), numNodes);
        if (numPacketsReceived >= numPackets && numPacketsReceived == numPacketsSent) {
            if (areAllTxBuffersEmpty(master, nodes.data(), numNodes, false)) {
                break;
            }
        }
    }
    assert(areAllTxBuffersEmpty(master, nodes.data(), numNodes, true));
    checkAllPacketsReceived(&checker);

    for (uint32_t i = 1; i < numNodes + 1; ++i) {
        freeNode(nodes[i]);
    }
    freeMaster(master);
}

static void test_full_system_with_master_restart(uint32_t numNodes,
                                                  bool singleChannel) noexcept {
    std::printf("test_full_system_with_master_restart, numNodes:%u\n", numNodes);
    PacketChecker checker{};
    TestMaster* master = nullptr;
    std::array<TestNode*, kMaxNodes> nodes{};
    initSystem(&checker, &master, nodes.data(), numNodes, singleChannel);
    runUntilAllNodesOnNetwork(&master, nodes.data(), numNodes, disableAllocationLogging,
                              singleChannel);

    std::array<uint32_t, kMaxNodes> packetsSent{};
    uint32_t numPacketsSent = 0;

    // Frames will be dropped over the restart, so use a throwaway checker here.
    PacketChecker notUsedChecker{};
    initPacketChecker(&notUsedChecker);

    for (int i = 0; i < 100; ++i) {
        fillTxBuffersWithRandomPackets(200, &notUsedChecker, master, nodes.data(), numNodes,
                                       packetsSent.data(), &numPacketsSent, false, false, false);
        run(master, &nodes[1], nullptr, numNodes, 1, false, singleChannel);
        processAllRxData(&notUsedChecker, master, nodes.data(), numNodes);
    }

    // Restart the master.
    freeMaster(master);
    master = createMaster(singleChannel);

    std::array<bool, kMaxNodes> disconnected{};
    for (int i = 0; i < 2000; ++i) {
        run(master, &nodes[1], nullptr, numNodes, 1, true, false);

        for (NodeId node = 1; node < numNodes + 1; ++node) {
            if (nodes[node]->nodeId == kUnallocatedNode) {
                disconnected[node] = true;
            }
        }

        uint32_t numConnected = 0;
        std::array<uint8_t, kMaxNodes / 8> connectedBitfield{};
        master->getConnectedNodes(connectedBitfield.data());
        for (uint32_t byte = 0; byte < kMaxNodes / 8; ++byte) {
            for (uint32_t bit = 0; bit < 8; ++bit) {
                if (0x1 & (connectedBitfield[byte] >> bit)) {
                    numConnected++;
                }
            }
        }
        if (numConnected == numNodes + 1) {
            break;
        }
    }

    for (NodeId node = 1; node < numNodes + 1; ++node) {
        assert(disconnected[node]);
    }

    {
        uint32_t innerNumPacketsSent = 0;
        uint32_t numPacketsReceived = 0;
        const uint32_t numPackets = 400;
        std::array<uint32_t, kMaxNodes> innerPacketsSent{};
        for (int i = 0; i < 1500; ++i) {
            fillTxBuffersWithRandomPackets(numPackets, &checker, master, nodes.data(), numNodes,
                                           innerPacketsSent.data(), &innerNumPacketsSent, false,
                                           false, false);
            run(master, &nodes[1], nullptr, numNodes, 1, false, false);
            numPacketsReceived += processAllRxData(&checker, master, nodes.data(), numNodes);
            if (numPacketsReceived >= numPackets && numPacketsReceived == innerNumPacketsSent) {
                if (areAllTxBuffersEmpty(master, nodes.data(), numNodes, false)) {
                    break;
                }
            }
        }
    }

    assert(areAllTxBuffersEmpty(master, nodes.data(), numNodes, true));
    checkAllPacketsReceived(&checker);

    for (uint32_t i = 1; i < numNodes + 1; ++i) {
        freeNode(nodes[i]);
    }
    freeMaster(master);
}

static void test_full_system_with_node_restart(uint32_t numNodes, bool singleChannel) noexcept {
    std::printf("test_full_system_with_node_restart, numNodes:%u\n", numNodes);
    PacketChecker checker{};
    TestMaster* master = nullptr;
    std::array<TestNode*, kMaxNodes> nodes{};
    initSystem(&checker, &master, nodes.data(), numNodes, singleChannel);
    runUntilAllNodesOnNetwork(&master, nodes.data(), numNodes, disableAllocationLogging,
                              singleChannel);

    std::array<uint32_t, kMaxNodes> packetsSent{};
    uint32_t numPacketsSent = 0;

    PacketChecker notUsedChecker{};
    initPacketChecker(&notUsedChecker);

    for (int i = 0; i < 200; ++i) {
        fillTxBuffersWithRandomPackets(200, &notUsedChecker, master, nodes.data(), numNodes,
                                       packetsSent.data(), &numPacketsSent, false, false, true);
        run(master, &nodes[1], nullptr, numNodes, 1, false, singleChannel);
        processAllRxData(&notUsedChecker, master, nodes.data(), numNodes);
    }

    MB_LOG("Restarting Nodes\n");
    for (uint32_t i = 1; i < numNodes + 1; ++i) {
        freeNode(nodes[i]);
        nodes[i] = createNode(0);
    }

    runUntilAllNodesOnNetwork(&master, nodes.data(), numNodes, false, singleChannel);
    processAllRxData(&notUsedChecker, master, nodes.data(), numNodes);

    {
        uint32_t innerNumPacketsSent = 0;
        uint32_t numPacketsReceived = 0;
        const uint32_t numPackets = 400;
        std::array<uint32_t, kMaxNodes> innerPacketsSent{};
        for (int i = 0; i < 1500; ++i) {
            fillTxBuffersWithRandomPackets(numPackets, &checker, master, nodes.data(), numNodes,
                                           innerPacketsSent.data(), &innerNumPacketsSent, false,
                                           false, false);
            run(master, &nodes[1], nullptr, numNodes, 1, false, singleChannel);
            numPacketsReceived += processAllRxData(&checker, master, nodes.data(), numNodes);
            if (numPacketsReceived >= numPackets && numPacketsReceived == innerNumPacketsSent) {
                if (areAllTxBuffersEmpty(master, nodes.data(), numNodes, false)) {
                    break;
                }
            }
        }
    }

    assert(areAllTxBuffersEmpty(master, nodes.data(), numNodes, true));
    checkAllPacketsReceived(&checker);

    for (uint32_t i = 1; i < numNodes + 1; ++i) {
        freeNode(nodes[i]);
    }
    freeMaster(master);
}

static void test_full_system_with_nodes_disconnecting(uint32_t numNodes,
                                                       bool singleChannel) noexcept {
    std::printf("test_full_system_with_nodes_disconnecting, numNodes:%u\n", numNodes);
    PacketChecker checker{};
    TestMaster* master = nullptr;
    std::array<TestNode*, kMaxNodes> nodes{};
    initSystem(&checker, &master, nodes.data(), numNodes, singleChannel);
    runUntilAllNodesOnNetwork(&master, nodes.data(), numNodes, disableAllocationLogging,
                              singleChannel);

    std::array<uint32_t, kMaxNodes> packetsSent{};
    uint32_t numPacketsSent = 0;

    PacketChecker notUsedChecker{};
    initPacketChecker(&notUsedChecker);

    for (int i = 0; i < 200; ++i) {
        fillTxBuffersWithRandomPackets(200, &notUsedChecker, master, nodes.data(), numNodes,
                                       packetsSent.data(), &numPacketsSent, false, false, false);
        run(master, &nodes[1], nullptr, numNodes, 1, false, singleChannel);
        processAllRxData(&notUsedChecker, master, nodes.data(), numNodes);
    }

    for (int z = 0; z < 10; ++z) {
        std::array<bool, kMaxNodes> ignoreNodes{};
        for (uint32_t i = 1; i < numNodes + 1; ++i) {
            if (std::rand() % 2) {
                ignoreNodes[i] = true;
            }
        }

        const auto numFramesToRun = static_cast<uint32_t>(std::rand() % 20000);
        for (uint32_t i = 0; i < numFramesToRun; ++i) {
            fillTxBuffersWithRandomPackets(200, &notUsedChecker, master, nodes.data(), numNodes,
                                           packetsSent.data(), &numPacketsSent, false, false, false);
            run(master, &nodes[1], ignoreNodes.data(), numNodes, 1, true, singleChannel);
            processAllRxData(&notUsedChecker, master, nodes.data(), numNodes);
        }
    }

    // Run long enough for any straggling nodes to leave the network.
    run(master, &nodes[1], nullptr, numNodes, 3000, true, false);
    assert(areAllTxBuffersEmpty(master, nodes.data(), numNodes, true));

    runUntilAllNodesOnNetwork(&master, nodes.data(), numNodes, disableAllocationLogging,
                              singleChannel);

    {
        uint32_t innerNumPacketsSent = 0;
        uint32_t numPacketsReceived = 0;
        const uint32_t numPackets = 400;
        std::array<uint32_t, kMaxNodes> innerPacketsSent{};
        for (int i = 0; i < 4000; ++i) {
            fillTxBuffersWithRandomPackets(numPackets, &checker, master, nodes.data(), numNodes,
                                           innerPacketsSent.data(), &innerNumPacketsSent, false,
                                           false, false);
            run(master, &nodes[1], nullptr, numNodes, 1, false, singleChannel);
            numPacketsReceived += processAllRxData(&checker, master, nodes.data(), numNodes);
            if (numPacketsReceived >= numPackets && numPacketsReceived == innerNumPacketsSent) {
                if (areAllTxBuffersEmpty(master, nodes.data(), numNodes, false)) {
                    break;
                }
            }
        }
    }

    assert(areAllTxBuffersEmpty(master, nodes.data(), numNodes, true));
    checkAllPacketsReceived(&checker);

    for (uint32_t i = 1; i < numNodes + 1; ++i) {
        freeNode(nodes[i]);
    }
    freeMaster(master);
}

void testMicrobus() noexcept {
    std::printf("Tests Start\n");

    // Half-duplex (single channel).
    test_fully_loaded_system(1, 1000, 4000, 0, 70, true, false, true);
    test_fully_loaded_system(1, 1000, 4000, 40, 40, true, false, false);
    test_fully_loaded_system(1, 1000, 4000, 60, 0, true, true, false);
    test_fully_loaded_system(4, 1000, 4000, 40, 0, true, true, false);

    // Full-duplex (dual channel).
    test_fully_loaded_system(1, 1000, 2000, 95, 95, false, false, false);
    test_fully_loaded_system(10, 4000, 3000, 80, 95, false, false, false);

    test_full_system_with_master_restart(1, false);
    test_full_system_with_master_restart(10, false);

    test_full_system_with_node_restart(1, false);
    test_full_system_with_node_restart(10, false);

    test_full_system_with_nodes_disconnecting(10, false);
    test_full_system_with_nodes_disconnecting(62, false);

    test_full_system_with_rx_buffer_overflows(1, 1000, 2000, false);
    std::printf("Tests Passed\n");
}

}  // namespace microbus::test
