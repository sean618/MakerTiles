// Copyright (c) 2025 Sean Bremner
// Licensed under the MIT License. See LICENSE file for details.

#pragma once

#include <cstdint>

#include "microbus/src/config.hpp"  // fixed-width integer type aliases

namespace microbus {

struct NodeStats {
    // Transmit
    uint64_t txPackets = 0;
    uint64_t txDataPackets = 0;
    uint64_t txBufferFull = 0;
    uint64_t txWindowRestarts = 0;

    // Receive
    uint64_t emptyRx = 0;
    uint64_t rxValid = 0;
    uint64_t rxPacketEntries = 0;
    uint64_t rxNodePackets = 0;
    uint64_t rxDataPackets = 0;
    uint64_t rxCrcFailures = 0;
    uint64_t rxInvalidProtocol = 0;
    uint64_t rxInvalidDataSize = 0;
    uint64_t rxInvalidPacketType = 0;
    uint64_t rxBufferFull = 0;

    // Membership
    uint32_t nodeLeftNw = 0;
    uint32_t nodeJoinedNw = 0;
    uint32_t networkFullCount = 0;
    uint32_t newNodeRequest = 0;      // node side
    uint32_t newNodeRequestRx = 0;    // master side
    uint32_t newNodeAllocated = 0;    // master side
    uint32_t newNodeAllocatedRx = 0;  // node side
};

}  // namespace microbus
