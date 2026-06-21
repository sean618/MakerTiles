/*
 * Copyright © 2024 - 2024 Sean Bremner. All rights reserved.>>
 */

#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "../src/fpCommon.hpp"
#include "../src/fpDaemon.hpp"
#include "../src/fpMaster.hpp"
#include "../src/fpNode.hpp"
#include "testCommon.hpp"

// TODO: test requestAck once that is implemented.

using namespace fp;

// ============================================ //

void testBoardInfoSending() {
    BoardInfo txboardInfo;
    createRandomBoardInfo(&txboardInfo, fpTestRand() % 200);

    OutgoingResponseState state{};
    state.requestId = static_cast<RequestId>(fpTestRand());
    state.startField = 0;
    state.numFields = 4;

    RawBusFieldPacket rawTxPacket{};
    BusFieldPacket txPacket{.p = &rawTxPacket, .dataSize = 0};
    txBoardInfoResponse(txboardInfo, state, *txPacket.p, txPacket.dataSize);

    RawDaemonFieldPacket rawRxPacket{};
    DaemonFieldPacket rxPacket{.p = &rawRxPacket, .dataSize = 0};
    rxPacket.dataSize = txPacket.dataSize;
    std::memcpy(&rxPacket.p->bp, txPacket.p, rxPacket.dataSize + kFpBusHeaderBytes);

    BoardInfo rxboardInfo{};
    bool result = fpRxBoardInfoResponse(rxPacket, rxboardInfo);
    assert(result == true);
    checkBoardInfoEquivalent(&txboardInfo, &rxboardInfo);
}

void testFieldInfoSending() {
    TestArena arena;  // backs the random field's byte storage; frees on return
    for (uint32_t i = 0; i < 20; i++) {
        FieldEntry txFieldInfo;
        createRandomFieldEntry(arena, &txFieldInfo, 1000);
        FieldTable table{.fields = &txFieldInfo, .numFields = 1};
        FieldTable* tables[] = {&table};

        OutgoingResponseState state{};
        state.requestId = static_cast<RequestId>(fpTestRand());
        state.startField = 0;
        state.numFields = 1;

        FpBoard board{};
        board.tables = tables;
        board.numTables = 1;

        RawBusFieldPacket rawTxPacket{};
        BusFieldPacket txPacket{.p = &rawTxPacket, .dataSize = 0};
        txFieldInfoResponse(board, state, *txPacket.p, txPacket.dataSize);

        RawDaemonFieldPacket rawRxPacket{};
        DaemonFieldPacket rxPacket{.p = &rawRxPacket, .dataSize = 0};
        rxPacket.dataSize = txPacket.dataSize;
        std::memcpy(&rxPacket.p->bp, txPacket.p, rxPacket.dataSize + kFpBusHeaderBytes);

        FieldEntry rxFieldInfo;
        bool result = fpRxFieldInfoResponse(rxPacket, rxFieldInfo);
        assert(result == true);
        checkFieldInfoEquivalent(&txFieldInfo, &rxFieldInfo);
    }
}

// Test sendFields and updateFields (using raw data pointers).
void testSendAndUpdateFieldsFromRawData() {
    // Create a board with fields that should fit in a sendFields packet.
    constexpr uint8_t kFieldBSpan = 20;
    FieldEntry fields[] = {
        //  ptr,    name,         span,        type,                size, flags,                                    setFieldFn, getFieldFn, units
        {nullptr, "field_A", 1, FieldDataType::Uint, 4, FieldFlags::Gettable | FieldFlags::Settable, nullptr, nullptr, ""},
        {nullptr, "field_B", kFieldBSpan, FieldDataType::Uint, 1, FieldFlags::Gettable | FieldFlags::Settable, nullptr, nullptr, ""},
        {nullptr, "field_C", 1, FieldDataType::Uint, 2, FieldFlags::Gettable | FieldFlags::Settable, nullptr, nullptr, ""},
    };
    FieldTable fieldTable{.fields = fields, .numFields = sizeof(fields) / sizeof(FieldEntry)};
    FieldTable* tables[] = {&fieldTable};
    FpBoard fp{};
    fp.boardInfo.numFields = fieldTable.numFields;
    fp.tables = tables;
    fp.numTables = 1;
    const uint32_t numDataBytes = 4 + kFieldBSpan + 2;
    uint8_t txFieldData[numDataBytes];
    for (uint32_t i = 0; i < kFieldBSpan; i++) {
        txFieldData[i] = i;
    }

    OutgoingResponseState state{.command = Command::None,
                          .requestId = static_cast<RequestId>(fpTestRand()),
                          .startField = 0,
                          .numFields = static_cast<FieldIndex>(2 + kFieldBSpan)};
    RawBusFieldPacket rawTxPacket{};
    BusFieldPacket txPacket{.p = &rawTxPacket, .dataSize = 0};
    RawData rawData;
    rawData.data = txFieldData;
    rawData.pos = 0;
    rawData.maxSize = numDataBytes;

    sendFieldsTxPacket(false, fp, state, *txPacket.p, txPacket.dataSize, &rawData);

    uint8_t rxFieldData[numDataBytes];
    RawData rxRawData;
    rxRawData.data = rxFieldData;
    rxRawData.pos = 0;
    rxRawData.maxSize = numDataBytes;

    updateFields(false, &fp, *txPacket.p, txPacket.dataSize, &rxRawData);

    for (uint32_t i = 0; i < kFieldBSpan; i++) {
        assert(rxFieldData[i] == i);
    }
}

// Test sendFieldsTxPacket and updateFields (using the field pointers).
void testSendAndUpdateFieldsFromTables() {
    // Create a board with fields that should fit in a sendFieldsTxPacket packet.
    constexpr uint8_t kFieldBSpan2 = 30;
    uint32_t fieldA1 = 12345;
    uint8_t fieldB1[kFieldBSpan2] = {0};
    for (uint32_t i = 0; i < kFieldBSpan2; i++) {
        fieldB1[i] = i;
    }
    uint16_t fieldC1 = 5;
    FieldEntry fields1[] = {
        {&fieldA1, "field_A", 1, FieldDataType::Uint, 4, FieldFlags::Gettable | FieldFlags::Settable, nullptr, nullptr, ""},
        {&fieldB1, "field_B", kFieldBSpan2, FieldDataType::Uint, 1, FieldFlags::Gettable | FieldFlags::Settable, nullptr, nullptr, ""},
        {&fieldC1, "field_C", 1, FieldDataType::Uint, 2, FieldFlags::Gettable | FieldFlags::Settable, nullptr, nullptr, ""},
    };
    FieldTable field1Table{.fields = fields1, .numFields = sizeof(fields1) / sizeof(FieldEntry)};
    FieldTable* tables1[] = {&field1Table};
    FpBoard fp1{};
    fp1.boardInfo.numFields = field1Table.numFields;
    fp1.tables = tables1;
    fp1.numTables = 1;

    // Create the one packet.
    RawBusFieldPacket rawTxPacket{};
    BusFieldPacket txPacket{.p = &rawTxPacket, .dataSize = 0};
    OutgoingResponseState state{.command = Command::None,
                          .requestId = static_cast<RequestId>(fpTestRand()),
                          .startField = 0,
                          .numFields = static_cast<FieldIndex>(2 + kFieldBSpan2)};

    sendFieldsTxPacket(false, fp1, state, *txPacket.p, txPacket.dataSize, nullptr);

    uint32_t fieldA2 = 0;
    uint8_t fieldB2[kFieldBSpan2] = {0};
    uint16_t fieldC2 = 0;
    FieldEntry fields2[] = {
        {&fieldA2, "field_A", 1, FieldDataType::Uint, 4, FieldFlags::Gettable | FieldFlags::Settable, nullptr, nullptr, ""},
        {&fieldB2, "field_B", kFieldBSpan2, FieldDataType::Uint, 1, FieldFlags::Gettable | FieldFlags::Settable, nullptr, nullptr, ""},
        {&fieldC2, "field_C", 1, FieldDataType::Uint, 2, FieldFlags::Gettable | FieldFlags::Settable, nullptr, nullptr, ""},
    };
    FieldTable fieldTable2{.fields = fields2, .numFields = sizeof(fields2) / sizeof(FieldEntry)};
    FieldTable* tables2[] = {&fieldTable2};
    FpBoard fp2{};
    fp2.boardInfo.numFields = fieldTable2.numFields;
    fp2.tables = tables2;
    fp2.numTables = 1;

    // Update based on the one sendFieldsTxPacket packet.
    updateFields(false, &fp2, *txPacket.p, txPacket.dataSize, nullptr);

    assert(fieldA1 == fieldA2);
    assert(fieldC1 == fieldC2);
    for (uint32_t i = 0; i < kFieldBSpan2; i++) {
        assert(fieldB1[i] == fieldB2[i]);
    }
}

// =================================== //

void testBasic() {
    testBoardInfoSending();
    testFieldInfoSending();
    testSendAndUpdateFieldsFromRawData();
    testSendAndUpdateFieldsFromTables();
}
