/*
 * Copyright © 2024 - 2024 Sean Bremner. All rights reserved.>>
 */

#pragma once

#include <cstdint>

#include "networkSim.hpp"

void masterOnlyTest(NetworkSim& sim, bool useRandomBoards, uint32_t numIterations);
void multiNodeTest(NetworkSim& sim, bool useRandomBoards, uint32_t numNodes, uint32_t numIterations);
void multiNodeWithDisconnectsTest(NetworkSim& sim, bool useRandomBoards, uint32_t numNodes,
                                  uint32_t numIterations);
