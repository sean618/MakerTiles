/*
 * Copyright © 2024 - 2024 Sean Bremner. All rights reserved.>>
 */

#include "testCommon.hpp"

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <memory>
#include <random>
#include <vector>

#include "../src/fpCommon.hpp"

using namespace fp;

// =================================== //
// Reproducible RNG

namespace {
std::mt19937 gRng;
unsigned gSeed = 0;
}  // namespace

void fpSeedTestRng(unsigned seed) {
    gSeed = seed;
    gRng.seed(seed);
}

unsigned fpTestRngSeed() {
    return gSeed;
}

unsigned fpInitTestRng() {
    unsigned seed;
    if (const char* env = std::getenv("FP_TEST_SEED")) {
        seed = static_cast<unsigned>(std::strtoul(env, nullptr, 10));
    } else {
        seed = static_cast<unsigned>(std::time(nullptr));
    }
    fpSeedTestRng(seed);
    return seed;
}

int fpTestRand() {
    return static_cast<int>(gRng() & 0x7fffffffu);
}

// =================================== //

std::string createRandomString(uint32_t len) {
    std::string s;
    s.reserve(len);
    for (uint32_t i = 0; i < len; i++) {
        char c = static_cast<char>('a' + (fpTestRand() % 26));
        if (c == 0) {
            c = 's';
        }
        s.push_back(c);
    }
    return s;
}

void checkBoardInfoEquivalent(BoardInfo* boardInfo1, BoardInfo* boardInfo2) {
    assert(boardInfo1->boardName == boardInfo2->boardName);
    assert(boardInfo1->customName == boardInfo2->customName);
    assert(boardInfo1->numFields == boardInfo2->numFields);
    assert(boardInfo1->uniqueId == boardInfo2->uniqueId);
}

void checkFieldInfoEquivalent(FieldEntry* fieldInfo1, FieldEntry* fieldInfo2) {
    assert(fieldInfo1->name == fieldInfo2->name);
    assert(fieldInfo1->units == fieldInfo2->units);
    assert(fieldInfo1->span == fieldInfo2->span);
    assert(fieldInfo1->type == fieldInfo2->type);
    assert(fieldInfo1->size == fieldInfo2->size);
    assert(fieldInfo1->flags == fieldInfo2->flags);
}

void checkBoardEquivalent(BoardInfo* boardInfo1, BoardInfo* boardInfo2, FieldTable* tables1[],
                          uint8_t numTables1, FieldTable* table2) {
    checkBoardInfoEquivalent(boardInfo1, boardInfo2);
    // Boards can have a different number of tables - just as long as the fields are all correct and in order.
    FieldIndex numFields1 = 0;
    for (uint32_t t = 0; t < numTables1; t++) {
        numFields1 += tables1[t]->numFields;
    }
    assert(numFields1 == table2->numFields);

    uint8_t t1 = 0;
    FieldIndex f1 = 0;
    for (FieldIndex f = 0; f < numFields1; f++) {
        if (f1 >= tables1[t1]->numFields) {
            t1++;
            f1 = 0;
        }
        checkFieldInfoEquivalent(&tables1[t1]->fields[f1], &table2->fields[f]);
        f1++;
    }
}

void createRandomBoardInfo(BoardInfo* boardInfo, FieldIndex numFields) {
    boardInfo->boardName = createRandomString(2 + fpTestRand() % 40);
    boardInfo->customName = createRandomString(2 + fpTestRand() % 40);
    boardInfo->numFields = numFields;
    boardInfo->uniqueId = ((static_cast<uint64_t>(fpTestRand())) << 32) | fpTestRand();
}

void createRandomFieldEntry(TestArena& arena, FieldEntry* fieldInfo, FieldIndex numFieldEntries) {
    uint32_t maxFields = (1u << (8 * sizeof(FieldIndex))) - 1;
    int32_t maxSpan = maxFields / numFieldEntries;
    fieldInfo->span = ((fpTestRand() % 10) == 0) ? fpTestRand() % maxSpan : 1;
    if (fieldInfo->span == 0) {
        fieldInfo->span = 1;
    }
    fieldInfo->type = static_cast<FieldDataType>(fpTestRand() % kMaxNumNodes);
    fieldInfo->size = 1 + (fpTestRand() % 8);
    const uint8_t flagBits =
        static_cast<uint8_t>((fpTestRand() % 2) | ((fpTestRand() % 2) << 1));  // gettable/settable
    fieldInfo->flags = static_cast<FieldFlags>(flagBits);
    fieldInfo->name = createRandomString(1 + fpTestRand() % 40);
    fieldInfo->units = createRandomString(fpTestRand() % 10);
    fieldInfo->ptr = arena.allocBytes(static_cast<size_t>(fieldInfo->span) * fieldInfo->size);
    for (uint32_t i = 0; i < static_cast<uint32_t>(fieldInfo->span) * fieldInfo->size; i++) {
        static_cast<uint8_t*>(fieldInfo->ptr)[i] = static_cast<uint8_t>(fpTestRand());
    }
    fieldInfo->getFieldFn = nullptr;
    fieldInfo->setFieldFn = nullptr;
}

// =================================== //
// Circular buffer functions

void initTxBuffer(TestTxBuffer* txBuffer, uint8_t* data, uint16_t packetSize, uint32_t size,
                  uint8_t* txDstNodeIds) {
    *txBuffer = TestTxBuffer{};
    txBuffer->data = data;
    txBuffer->packetSize = packetSize;
    txBuffer->size = size;
    txBuffer->txDstNodeIds = txDstNodeIds;
}
void initRxBuffer(TestRxBuffer* rxBuffer, uint8_t* data, uint16_t packetSize, uint32_t size,
                  uint8_t* rxSrcNodeIds) {
    *rxBuffer = TestRxBuffer{};
    rxBuffer->data = data;
    rxBuffer->packetSize = packetSize;
    rxBuffer->size = size;
    rxBuffer->rxSrcNodeIds = rxSrcNodeIds;
}

uint8_t* testAllocateTxPacket(TestTxBuffer& buffer) {
    assert(buffer.packetAllocated == false);
    if (circularBufferFull(buffer.start, buffer.end, buffer.size)) {
        return nullptr;
    }
    buffer.packetAllocated = true;
    uint8_t* data = &buffer.data[((buffer.packetSize + 2) * buffer.end)];
    return &data[2];
}
void testSubmitAllocatedTxPacket(TestTxBuffer& buffer, uint8_t dstNodeId, uint16_t packetSize) {
    assert(buffer.packetAllocated);
    assert(!circularBufferFull(buffer.start, buffer.end, buffer.size));
    if (buffer.txDstNodeIds != nullptr) {
        buffer.txDstNodeIds[buffer.end] = dstNodeId;
    }
    uint8_t* data = &buffer.data[((buffer.packetSize + 2) * buffer.end)];
    std::memcpy(data, &packetSize, 2);
    buffer.end = incrAndWrap(buffer.end, 1, buffer.size);
    buffer.packetAllocated = false;
}

uint8_t* testPeekRxPacket(TestRxBuffer& buffer, uint16_t& numBytes, uint8_t& srcNodeId) {
    if (circularBufferEmpty(buffer.start, buffer.end, buffer.size)) {
        return nullptr;
    }
    uint8_t* data = &buffer.data[(buffer.packetSize + 2) * buffer.start];
    if (buffer.rxSrcNodeIds) {
        srcNodeId = buffer.rxSrcNodeIds[buffer.start];
    }
    std::memcpy(&numBytes, data, 2);
    return &data[2];
}
bool testPopRxPacket(TestRxBuffer& buffer) {
    assert(!circularBufferEmpty(buffer.start, buffer.end, buffer.size));
    buffer.start = incrAndWrap(buffer.start, 1, buffer.size);
    return true;
}

// Return whether tx was successful.
bool transferTxToRx(bool daemonToMasterInterface, TestTxBuffer* txBuffer, TestRxBuffer* rxBuffer) {
    if (!circularBufferEmpty(txBuffer->start, txBuffer->end, txBuffer->size)) {
        if (!circularBufferFull(rxBuffer->start, rxBuffer->end, rxBuffer->size)) {
            uint8_t* txData = &txBuffer->data[(txBuffer->packetSize + 2) * txBuffer->start];
            uint8_t* rxData = &rxBuffer->data[(rxBuffer->packetSize + 2) * rxBuffer->end];
            std::memcpy(rxData, txData, rxBuffer->packetSize + 2);
            rxBuffer->end = incrAndWrap(rxBuffer->end, 1, rxBuffer->size);
            txBuffer->start = incrAndWrap(txBuffer->start, 1, txBuffer->size);
            return true;
        } else {
            assert(!daemonToMasterInterface);
        }
    }
    return false;
}

// =================================== //
// TestInterface adapters

uint8_t* TestInterface::allocateTxPacket() {
    return testAllocateTxPacket(*txBuffer);
}
void TestInterface::submitTxPacket(uint8_t dstNodeId, uint16_t packetSize) {
    testSubmitAllocatedTxPacket(*txBuffer, dstNodeId, packetSize);
}
uint8_t* TestInterface::peekRxPacket(uint16_t& numBytes, uint8_t& srcNodeId) {
    return testPeekRxPacket(*rxBuffer, numBytes, srcNodeId);
}
bool TestInterface::popRxPacket() {
    return testPopRxPacket(*rxBuffer);
}
