// Copyright (c) 2025 Sean Bremner
// Licensed under the MIT License. See LICENSE file for details.

#include "microbus/test/packet_checker.hpp"

#include <cassert>
#include <cstdlib>
#include <cstring>

namespace microbus::test {

void initPacketChecker(PacketChecker* checker) noexcept {
    checker->currentId = 1;  // ids start at 1; 0 means "invalid"
}

uint8_t* createTxPacket(PacketChecker* checker, NodeId srcSimId,
                             NodeId dstSimId, uint16_t size, bool mightBeDropped) noexcept {
    MB_ASSERT(size <= kMaxDataSize, "checker packet too large");
    MB_ASSERT(size >= kCheckerHeader, "checker packet smaller than header");

    for (int i = 0; i < kMaxCheckerPackets; ++i) {
        CheckerPacket* pkt = &checker->txPackets[i];
        if (!pkt->valid) {
            pkt->valid = true;
            pkt->mightBeDropped = mightBeDropped;
            pkt->size = size;
            pkt->id = checker->currentId;
            pkt->srcSimId = srcSimId;
            pkt->dstSimId = dstSimId;
            pkt->data[0] = srcSimId;
            pkt->data[1] = dstSimId;
            std::memcpy(&pkt->data[2], &pkt->id, 4);
            for (uint32_t j = 0; j + kCheckerHeader < size; ++j) {
                pkt->data[6 + j] = static_cast<uint8_t>(std::rand() % 256);
            }
            checker->currentId++;
            return pkt->data.data();
        }
    }
    MB_ASSERT(false, "checker out of free packet slots");
    return nullptr;
}

void processRxPacket(PacketChecker* checker, const uint8_t* data, uint16_t size) noexcept {
    uint32_t packetId = 0;
    const NodeId srcSimId = data[0];
    const NodeId dstSimId = data[1];
    std::memcpy(&packetId, &data[2], 4);

    for (int i = 0; i < kMaxCheckerPackets; ++i) {
        CheckerPacket* pkt = &checker->txPackets[i];
        if (pkt->valid && pkt->id == packetId && pkt->srcSimId == srcSimId &&
            pkt->dstSimId == dstSimId) {
            MB_ASSERT(pkt->size == size, "checker: rx size mismatch");
            for (uint32_t j = 0; j < size; ++j) {
                MB_ASSERT(pkt->data[j] == data[j], "checker: rx data mismatch");
            }
            checker->numCorrectRxPackets++;
            MB_LOG("Packet received %d -> %d, id:%d\n", srcSimId, dstSimId, packetId);
            pkt->valid = false;  // delivered
            return;
        }
    }
    std::printf("Failed - no packet matched: %d -> %d, id:%d\n", srcSimId, dstSimId, packetId);
    MB_ASSERT(false, "checker: rx packet matched no sent packet");
}

void checkAllPacketsReceived(PacketChecker* checker) noexcept {
    for (int i = 0; i < kMaxCheckerPackets; ++i) {
        CheckerPacket* pkt = &checker->txPackets[i];
        if (pkt->valid && !pkt->mightBeDropped) {
            std::printf("Failed - packet never received %d -> %d, id:%d\n", pkt->srcSimId,
                        pkt->dstSimId, pkt->id);
            MB_ASSERT(false, "checker: packet never received");
        }
    }
}

}  // namespace microbus::test
