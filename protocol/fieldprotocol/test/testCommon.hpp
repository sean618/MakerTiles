/*
 * Copyright © 2024 - 2024 Sean Bremner. All rights reserved.>>
 */

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "../src/fpCommon.hpp"

// =================================== //
// Allocation arena
//
// The random board/field generators need backing storage that outlives the
// generating call (the protocol holds raw pointers into it). The arena owns
// every such allocation in unique_ptr vectors, so its destructor frees them in
// one shot - no manual delete, nothing for AddressSanitizer / LeakSanitizer to
// flag. Callers give the arena the lifetime of the boards (a System owns one),
// so freeing happens by scope exit (RAII) rather than an explicit call.
struct TestArena {
    std::vector<std::unique_ptr<uint8_t[]>> bytes;
    std::vector<std::unique_ptr<fp::FieldTable>> tables;
    std::vector<std::unique_ptr<fp::FieldEntry[]>> entries;

    uint8_t* allocBytes(size_t n) {
        bytes.push_back(std::make_unique<uint8_t[]>(n));
        return bytes.back().get();
    }
    fp::FieldTable* allocTable() {
        tables.push_back(std::make_unique<fp::FieldTable>());
        return tables.back().get();
    }
    fp::FieldEntry* allocEntries(size_t n) {
        entries.push_back(std::make_unique<fp::FieldEntry[]>(n));
        return entries.back().get();
    }
};

// Circular-buffer-backed test transport. The two buffer structs hold the raw
// storage; TestInterface adapts them to the abstract fp::FpInterface used by
// the protocol code.
struct TestTxBuffer {
    uint32_t packetSize = 0;
    uint32_t start = 0;
    uint32_t end = 0;
    uint32_t size = 0;
    uint8_t* data = nullptr;
    bool packetAllocated = false;
    uint8_t* txDstNodeIds = nullptr;
};

struct TestRxBuffer {
    uint32_t packetSize = 0;
    uint32_t start = 0;
    uint32_t end = 0;
    uint32_t size = 0;
    uint8_t* data = nullptr;
    uint8_t* rxSrcNodeIds = nullptr;
};

class TestInterface : public fp::FpInterface {
public:
    uint8_t* allocateTxPacket() override;
    void submitTxPacket(uint8_t dstNodeId, uint16_t packetSize) override;
    uint8_t* peekRxPacket(uint16_t& numBytes, uint8_t& srcNodeId) override;
    bool popRxPacket() override;
    uint8_t getNodeId() const override { return nodeId; }

    TestTxBuffer* txBuffer = nullptr;
    TestRxBuffer* rxBuffer = nullptr;
    uint8_t nodeId = fp::kInvalidNodeId;
};

// =================================== //
// Reproducible RNG.
//
// All test randomness flows through fpTestRand() (a drop-in for std::rand())
// backed by a single seeded std::mt19937. fpInitTestRng() seeds it from the
// FP_TEST_SEED environment variable when set, otherwise from the clock, and
// returns the seed so the caller can log it - a failing random iteration is
// then replayed exactly with `FP_TEST_SEED=<n>`.

void fpSeedTestRng(unsigned seed);
unsigned fpTestRngSeed();
unsigned fpInitTestRng();
int fpTestRand();  // returns a value in [0, 0x7fffffff], like std::rand()

// =================================== //

std::string createRandomString(uint32_t len);
void checkBoardInfoEquivalent(fp::BoardInfo* boardInfo1, fp::BoardInfo* boardInfo2);
void checkFieldInfoEquivalent(fp::FieldEntry* fieldInfo1, fp::FieldEntry* fieldInfo2);
void checkBoardEquivalent(fp::BoardInfo* boardInfo1, fp::BoardInfo* boardInfo2, fp::FieldTable* tables1[],
                          uint8_t numTables1, fp::FieldTable* table2);
void createRandomBoardInfo(fp::BoardInfo* boardInfo, fp::FieldIndex numFields);
// Field byte storage is taken from `arena`, which must outlive the entry.
void createRandomFieldEntry(TestArena& arena, fp::FieldEntry* fieldInfo, fp::FieldIndex remainingEntries);

// =================================== //
// Test transport buffer helpers

void initTxBuffer(TestTxBuffer* txBuffer, uint8_t* data, uint16_t packetSize, uint32_t size,
                  uint8_t* txDstNodeIds);
void initRxBuffer(TestRxBuffer* rxBuffer, uint8_t* data, uint16_t packetSize, uint32_t size,
                  uint8_t* rxSrcNodeIds);
bool transferTxToRx(bool daemonToMasterInterface, TestTxBuffer* txBuffer, TestRxBuffer* rxBuffer);

// Lower-level buffer ops, shared with the usb mock's byte-stream framing.
uint8_t* testAllocateTxPacket(TestTxBuffer& buffer);
void testSubmitAllocatedTxPacket(TestTxBuffer& buffer, uint8_t dstNodeId, uint16_t packetSize);
uint8_t* testPeekRxPacket(TestRxBuffer& buffer, uint16_t& numBytes, uint8_t& srcNodeId);
bool testPopRxPacket(TestRxBuffer& buffer);
