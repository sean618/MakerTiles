// Copyright (c) 2025 Sean Bremner
// Licensed under the MIT License. See LICENSE file for details.
//
// A correctness oracle for the system tests. Every application payload handed
// to the bus is registered here (keyed by a small synthetic header); when the
// other end delivers it, we look it up and byte-compare. At the end of a test,
// any still-registered packet that was not marked "might be dropped" is a lost
// packet and fails the test.

#pragma once

#include <array>
#include <cstdint>

#include "microbus/src/microbus.hpp"

namespace microbus::test {

inline constexpr int kMaxCheckerPackets = 500;
// Synthetic per-payload header: srcSimId(1) + dstSimId(1) + id(4).
inline constexpr int kCheckerHeader = 6;

struct CheckerPacket {
    bool valid = false;
    bool mightBeDropped = false;
    uint32_t id = 0;
    uint16_t size = 0;
    NodeId srcSimId = 0;
    NodeId dstSimId = 0;
    std::array<uint8_t, kPacketSize> data{};
};

struct PacketChecker {
    uint32_t currentId = 1;  // 0 is reserved as "invalid"
    uint32_t numCorrectRxPackets = 0;
    uint32_t numPackets = 0;
    std::array<CheckerPacket, kMaxCheckerPackets> txPackets{};
};

void initPacketChecker(PacketChecker* checker) noexcept;
uint8_t* createTxPacket(PacketChecker* checker, NodeId srcSimId,
                             NodeId dstSimId, uint16_t size, bool mightBeDropped) noexcept;
void processRxPacket(PacketChecker* checker, const uint8_t* data, uint16_t size) noexcept;
void checkAllPacketsReceived(PacketChecker* checker) noexcept;

}  // namespace microbus::test
