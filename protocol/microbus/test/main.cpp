// Copyright (c) 2025 Sean Bremner
// Licensed under the MIT License. See LICENSE file for details.
//
// Test entry point. Defines the host-owned logging globals and runs the suite.
// Diagnostic logging is written to a file (MICROBUS_LOG below); stdout carries
// only the high-level test progress and the final "Tests Passed".

#include <cstdint>
#include <cstdio>

#include "microbus/src/microbus.hpp"

namespace microbus {
// Globals declared in diagnostics.hpp, defined here by the host.
std::FILE* logSink = nullptr;
bool loggingEnabled = false;  // diagnostics off by default; tests toggle as needed
uint64_t cycleIndex = 0;
}  // namespace microbus

namespace microbus::test {
void testNetworkManager() noexcept;
void testTxManager() noexcept;
void test_packets_to_from_each_node(uint32_t numNodes, uint32_t numPacketsPerNode) noexcept;
void testMicrobus() noexcept;
}  // namespace microbus::test

int main() {
    using namespace microbus;
    using namespace microbus::test;

    if (MICROBUS_LOGGING) {
        logSink = std::fopen("microbus_test_log.txt", "w");
        if (logSink == nullptr) {
            logSink = stderr;
        }
    }

    testNetworkManager();
    testTxManager();

    test_packets_to_from_each_node(2, 1);
    test_packets_to_from_each_node(9, 1);

    testMicrobus();

    if (logSink && logSink != stderr) {
        std::fclose(logSink);
    }
    return 0;
}
