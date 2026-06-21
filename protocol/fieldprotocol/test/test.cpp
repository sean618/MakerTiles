/*
 * Copyright © 2024 - 2024 Sean Bremner. All rights reserved.>>
 */

#include <cstdint>
#include <cstdio>

#include "../src/fpCommon.hpp"
#include "../src/fpDaemon.hpp"
#include "networkSim.hpp"
#include "testBasic.hpp"
#include "testCommon.hpp"
#include "testFullSystem.hpp"

// Runs the scenario suite over every available transport. The mock microbus is
// always present; the real microbus is only linked in non-mock builds.
static void runAllTests(bool randomBoards, uint32_t numBoards, uint32_t numIterations) {
    MockMicrobusSim mock;
    masterOnlyTest(mock, randomBoards, numIterations);
    multiNodeTest(mock, randomBoards, 2, numIterations);
    multiNodeTest(mock, randomBoards, numBoards, numIterations);

#ifndef USE_MOCK_MICROBUS
    RealMicrobusSim real;
    masterOnlyTest(real, randomBoards, numIterations);
    multiNodeTest(real, randomBoards, 2, numIterations);
    multiNodeTest(real, randomBoards, numBoards, numIterations);
    multiNodeWithDisconnectsTest(real, randomBoards, 2, numIterations);
#endif
}

int main() {
    std::printf("Start\n");

    // Seed all test randomness from FP_TEST_SEED (if set) or the clock, and log
    // the seed so a failing random iteration can be replayed with
    // FP_TEST_SEED=<n>.
    unsigned seed = fpInitTestRng();
    std::printf("Test seed: %u (set FP_TEST_SEED to reproduce)\n", seed);

    if constexpr (fp::kFpLogging != 0 || fp::kMicrobusLogging != 0) {
        fp::logfile = std::fopen("log.txt", "w+");
        std::fprintf(fp::logfile, "Start\n");
    }

    testBasic();

    uint32_t numIterations = 1;
    runAllTests(false, kMasterNodeSystemNumNodes - 1, 1);
    runAllTests(true, fp::kMaxFpDaemonBoards - 1, numIterations);

    std::printf("Passed\n");

    if (fp::kFpLogging) {
        std::fprintf(fp::logfile, "End\n");
        std::fclose(fp::logfile);
    }
}
