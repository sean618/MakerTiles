/*
 * Copyright © 2024 - 2024 Sean Bremner. All rights reserved.>>
 */

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "../src/fpCommon.hpp"
#include "../src/fpDaemon.hpp"
#include "../src/fpNode.hpp"
#include "testFullSystem.hpp"

#include "networkSim.hpp"
#include "testCommon.hpp"

using namespace fp;

// =================================== //

static void testMasterNodeOnly(System& sys) {
    // Run until the (single) board is connected.
    uint32_t iteration = 0;
    while (sys.daemon.board[0] == nullptr) {
        sys.daemon.updateConnectedBoards();
        iteration++;
        if (iteration == 10000) {
            assert(0);
        }
    }

    checkBoardEquivalent(&sys.boardInfo[0], &sys.daemon.board[0]->boardInfo, &sys.tables[0], 1,
                         sys.daemon.board[0]->tables[0]);

    FieldIndex startFieldIndex = 0;
    for (uint32_t j = 0; j < sys.daemon.board[0]->boardInfo.numFields; j++) {
        FieldEntry* field = &sys.tables[0]->fields[j];
        if (isGettable(field->flags)) {
            uint32_t dataSize = field->span * field->size;
            uint8_t* data = static_cast<uint8_t*>(std::malloc(dataSize));
            sys.daemon.getFields(sys.daemon.board[0]->nodeId, startFieldIndex, field->span, data, dataSize);
            for (uint32_t z = 0; z < dataSize; z++) {
                assert(data[z] == static_cast<uint8_t*>(field->ptr)[z]);
            }
            std::free(data);
        }
        startFieldIndex += field->span;
    }
    sys.daemon.deleteFieldProtocol();
}

// =============================================== //

// Returns the board state for the given nodeId. The master node (kMasterNodeId)
// is hosted by the Master object; every other node lives in the nodes array.
static FpBoard* getBoardForNodeId(System& sys, uint8_t nodeId) {
    if (nodeId == kMasterNodeId) {
        return &sys.master.board();
    }
    for (uint32_t n = 0; n < sys.numNodes + 1; n++) {
        if (sys.nodes[n].has_value() && sys.nodes[n]->board().nodeId == nodeId) {
            return &sys.nodes[n]->board();
        }
    }
    return nullptr;
}

static void runUntilAllBoardsDiscovered(System& sys) {
    for (uint32_t i = 0; i < sys.numNodes + 1; i++) {
        uint32_t iteration = 0;
        while (sys.daemon.board[i] == nullptr) {
            sys.daemon.updateConnectedBoards();
            iteration++;
            if (iteration == 10000) {
                assert(0);
            }
        }
    }
}

static void testFullSystem(System& sys) {
    runUntilAllBoardsDiscovered(sys);

    for (uint32_t i = 0; i < sys.numNodes + 1; i++) {
        auto* daemonBoard = sys.daemon.board[i].get();

        // Find the matching node.
        FpBoard* board = getBoardForNodeId(sys, daemonBoard->nodeId);
        assert(board);

        // Check that the daemon's version of the board matches.
        checkBoardEquivalent(&board->boardInfo, &daemonBoard->boardInfo, board->tables,
                             board->numTables, daemonBoard->tables[0]);

        FieldIndex startFieldIndex = 0;
        for (uint32_t t = 0; t < daemonBoard->numTables; t++) {
            FieldTable* table = board->tables[t];
            for (uint32_t j = 0; j < table->numFields; j++) {
                FieldEntry* field = &table->fields[j];
                uint32_t dataSize = field->span * field->size;
                uint8_t* data = static_cast<uint8_t*>(std::malloc(dataSize));

                // Test Get.
                if (isGettable(field->flags)) {
                    sys.daemon.getFields(daemonBoard->nodeId, startFieldIndex, field->span, data, dataSize);
                    for (uint32_t z = 0; z < dataSize; z++) {
                        assert(data[z] == static_cast<uint8_t*>(field->ptr)[z]);
                    }
                }

                // Test Set.
                uint8_t* newdata = static_cast<uint8_t*>(std::malloc(dataSize));
                for (uint32_t z = 0; z < dataSize; z++) {
                    newdata[z] = fpTestRand() % kMaxNumNodes;
                }
                // TODO: add request acks and then enable waitForResponse.
                sys.daemon.setFields(daemonBoard->nodeId, startFieldIndex, field->span, newdata, dataSize);

                for (uint32_t delay = 0; delay < 1000; delay++) {
                    sys.pump();
                }

                if (isGettable(field->flags)) {
                    for (uint32_t z = 0; z < dataSize; z++) {
                        assert(static_cast<uint8_t*>(field->ptr)[z] ==
                               (isSettable(field->flags) ? newdata[z] : data[z]));
                    }
                }
                std::free(newdata);
                std::free(data);
                startFieldIndex += field->span;
            }
        }
    }
    sys.daemon.deleteFieldProtocol();
}

static void testFullSystemWithNodeDisconnects(System& sys) {
    runUntilAllBoardsDiscovered(sys);

    uint32_t numExpActiveNodes = 0;
    for (uint32_t i = 1; i < sys.numNodes + 1; i++) {
        if (fpTestRand() % 2 == 0) {
            sys.ignoreNode[i] = true;
        } else {
            numExpActiveNodes++;
        }
    }

    for (uint32_t i = 0; i < 1000; i++) {
        sys.daemon.updateConnectedBoards();
    }

    uint32_t numActiveNodes = 0;
    for (uint32_t i = 1; i < sys.numNodes + 1; i++) {
        if (sys.daemon.board[i] != nullptr) {
            numActiveNodes++;
        }
    }
    assert(numExpActiveNodes == numActiveNodes);

    // Now re-enable them.
    for (uint32_t i = 1; i < sys.numNodes + 1; i++) {
        sys.ignoreNode[i] = false;
    }

    runUntilAllBoardsDiscovered(sys);

    for (uint32_t i = 0; i < sys.numNodes + 1; i++) {
        auto* daemonBoard = sys.daemon.board[i].get();

        // Find the matching node.
        FpBoard* board = getBoardForNodeId(sys, daemonBoard->nodeId);
        assert(board);

        // Check that the daemon's version of the board matches.
        checkBoardEquivalent(&board->boardInfo, &daemonBoard->boardInfo, board->tables,
                             board->numTables, daemonBoard->tables[0]);
    }
    sys.daemon.deleteFieldProtocol();
}

// =================================== //
// Scenario drivers
//
// Each constructs a fresh System per iteration (no shared global state), wires
// it to the given transport, points the daemon's delay pump at it, and runs.

void masterOnlyTest(NetworkSim& sim, bool useRandomBoards, uint32_t numIterations) {
    for (uint32_t i = 0; i < numIterations; i++) {
        fpPrintf("Starting %s master only iteration: %u\n", sim.name(), i);
        System sys;
        buildSystem(sys, sim, useRandomBoards, 0);
        sys.daemon.init(sim.daemonItf(), [s = &sys] { s->pump(); });
        testMasterNodeOnly(sys);
        std::printf("%s master only iteration: %u - Num cycles: %llu\n", sim.name(), i,
                    static_cast<unsigned long long>(sys.cycles));
    }
}

void multiNodeTest(NetworkSim& sim, bool useRandomBoards, uint32_t numNodes, uint32_t numIterations) {
    for (uint32_t i = 0; i < numIterations; i++) {
        fpPrintf("Starting %s full iteration with %u nodes: %u\n", sim.name(), numNodes, i);
        System sys;
        buildSystem(sys, sim, useRandomBoards, numNodes);
        sys.daemon.init(sim.daemonItf(), [s = &sys] { s->pump(); });
        testFullSystem(sys);
        std::printf("%s full iteration with %u nodes: %u - Num cycles: %llu\n", sim.name(), numNodes, i,
                    static_cast<unsigned long long>(sys.cycles));
    }
}

void multiNodeWithDisconnectsTest(NetworkSim& sim, bool useRandomBoards, uint32_t numNodes,
                                  uint32_t numIterations) {
    for (uint32_t i = 0; i < numIterations; i++) {
        fpPrintf("Starting %s full with disconnects iteration with %u nodes: %u\n", sim.name(), numNodes, i);
        System sys;
        buildSystem(sys, sim, useRandomBoards, numNodes);
        sys.daemon.init(sim.daemonItf(), [s = &sys] { s->pump(); });
        testFullSystemWithNodeDisconnects(sys);
        std::printf("%s full with disconnects iteration with %u nodes: %u - Num cycles: %llu\n", sim.name(),
                    numNodes, i, static_cast<unsigned long long>(sys.cycles));
    }
}
