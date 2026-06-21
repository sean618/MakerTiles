/*
 * Copyright © 2024 - 2024 Sean Bremner. All rights reserved.>>
 */

#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <vector>

#include "fpCommon.hpp"

namespace fp {

// Referenced by callers in every build (e.g. test bounds checks), so it lives
// outside the USE_FP_DAEMON guard below.
inline constexpr uint8_t kMaxFpDaemonBoards = 50;

// ==================================== //
// Implementations
//
// The daemon only exists in builds that opt in via USE_FP_DAEMON; in the shared
// library (which hosts the Python daemon instead) it compiles away to nothing.

#ifdef USE_FP_DAEMON

inline constexpr uint8_t kMaxRequestIds = 32;
inline constexpr uint8_t kTxBufferSize = 32;

using DelayFn = std::function<void()>;

struct Request {
    uint16_t id = 0;
    uint8_t dstNodeId = 0;
    Command command = Command::None;
    FieldIndex numFields = 0;
    FieldIndex fieldIndex = 0;
    RawData returnData{};
};

// A board as seen by the daemon. It derives from FpBoard (so it can be passed
// straight to the protocol core and inspected by the tests) and owns the field
// storage that is populated from the wire.
struct DaemonBoard : FpBoard {
    DaemonBoard() {
        tablePtr = &table;
        tables = &tablePtr;
        numTables = 1;
    }

    void allocateFields(FieldIndex count) {
        fieldStore.assign(count, FieldEntry{});
        table.fields = fieldStore.data();
        table.numFields = count;
    }

    FieldTable table{};
    FieldTable* tablePtr = nullptr;
    std::vector<FieldEntry> fieldStore;
};

// Managing the amount we send to the master so that we don't overflow its
// buffers. Mirrors the Python daemon's tx_manager: a single combined credit
// pool, one credit consumed per packet.
class DaemonTxManager {
public:
    void init(FpInterface& usb) {
        *this = DaemonTxManager{};
        usbItf = &usb;
    }

    // Try and send as many tx packets as possible (based on the tx buffer length
    // and the number of credits). Called whenever a packet is submitted and
    // whenever credits are returned.
    void processTx() {
        while ((numCredits > 0 || !creditsInitialised) &&
               !circularBufferEmpty(start, end, kTxBufferSize)) {
            uint8_t* packet = usbItf->allocateTxPacket();
            const uint32_t size = packetSize[start];
            std::memcpy(packet, &txBuffer[size_t{start} * kMaxDaemonPacketSize], kMaxDaemonPacketSize);
            start = incrAndWrap(start, 1, kTxBufferSize);
            if (creditsInitialised) {
                numCredits--;
            }
            usbItf->submitTxPacket(0, size);
        }
    }

    void setCredits(uint32_t numRxCredits, uint32_t numTxCredits) {
        fpAssert(numRxCredits > 0);
        fpAssert(numTxCredits > 0);
        fpAssert(!creditsInitialised);
        creditsInitialised = true;
        // Mirror the Python daemon (tx_manager.set_credits): a single combined pool.
        // The -2 accounts for the one rx and one tx credit consumed by the in-flight
        // get-credits request that delivered these values.
        maxCredits = numRxCredits + numTxCredits - 2;
        numCredits = maxCredits;
    }

    void returnCredits(uint8_t /*returnedRxCredits*/, uint8_t returnedTxCredits) {
        if (creditsInitialised) {
            // Mirror the Python daemon (tx_manager.return_credits): replenish the single
            // pool from returned tx credits only, capped at the initial maximum.
            if (returnedTxCredits > 0 && numCredits <= maxCredits) {
                numCredits += returnedTxCredits;
            }
        }
        processTx();
    }

    bool txBufferFull() const {
        return circularBufferFull(start, end, kTxBufferSize);
    }

    void allocateTxPacket(DaemonFieldPacket& txPacket) {
        fpAssert(!packetAllocated, "Only one allocated packet is allowed at any one time");
        fpAssert(!txBufferFull());
        packetAllocated = true;
        txPacket.p = reinterpret_cast<RawDaemonFieldPacket*>(&txBuffer[size_t{end} * kMaxDaemonPacketSize]);
        txPacket.dataSize = 0;
    }

    void submitAllocatedTxPacket(DaemonFieldPacket& txPacket) {
        fpAssert(packetAllocated);
        txPacket.p->packetSize = txPacket.dataSize + kFpDaemonHeaderBytes;
        packetSize[end] = txPacket.p->packetSize;
        end = incrAndWrap(end, 1, kTxBufferSize);
        packetAllocated = false;
        processTx();
    }

    bool peekRxPacket(DaemonFieldPacket& rxPacket) {
        uint8_t unused = 0;
        uint16_t numBytes = 0;
        rxPacket.p = reinterpret_cast<RawDaemonFieldPacket*>(usbItf->peekRxPacket(numBytes, unused));
        rxPacket.dataSize = numBytes - kFpDaemonHeaderBytes;
        return rxPacket.p != nullptr;
    }

    void popRxPacket() {
        usbItf->popRxPacket();
    }

    bool creditsInitialised = false;
    uint32_t start = 0;
    uint32_t end = 0;
    uint32_t numCredits = 0;
    uint32_t maxCredits = 0;
    std::array<uint8_t, size_t{kMaxDaemonPacketSize} * kTxBufferSize> txBuffer{};
    std::array<uint32_t, kTxBufferSize> packetSize{};
    bool packetAllocated = false;
    FpInterface* usbItf = nullptr;
};

// Response parsers - also exercised directly by the unit tests, so exposed here.
// Each returns true once the request is complete. Forward declared because the
// Daemon member functions below call them.
bool fpRxBoardInfoResponse(DaemonFieldPacket& rx, BoardInfo& boardInfo);
bool fpRxFieldInfoResponse(DaemonFieldPacket& rx, FieldEntry& fieldEntry);



class Daemon {
public:
    Daemon() = default;

    void init(FpInterface& itf, DelayFn delayFn) {
        *this = Daemon{};
        txManager.init(itf);
        delay = std::move(delayFn);
    }

    void processRx() {
        while (true) {
            DaemonFieldPacket rx;
            if (!txManager.peekRxPacket(rx)) {
                break;
            }

            if (rx.p->returnRxCredits > 0 || rx.p->returnTxCredits > 0) {
                txManager.returnCredits(rx.p->returnRxCredits, rx.p->returnTxCredits);
            }

            const RequestId requestId = rx.p->bp.requestId;
            const Command command = static_cast<Command>(rx.p->bp.command);
            const FieldIndex numFields = rx.p->bp.numFields;
            const uint8_t srcNodeId = rx.p->srcOrDstNodeId;

            if (command == Command::ReturningCredits) {
                // Do nothing.
            } else {
                DaemonBoard* boardPtr = findBoard(srcNodeId);
                Request* request = findRequest(srcNodeId, requestId);
                fpAssert(request != nullptr);

                switch (command) {
                    case Command::SendingConnectedNodes:
                        fpAssert(rx.dataSize == kMaxNumNodes / 8);
                        std::memcpy(request->returnData.data, rx.p->bp.data, kMaxNumNodes / 8);
                        requestCompleted(*request);
                        break;
                    case Command::SendingMaxBufferCredits:
                        txManager.setCredits(rx.p->bp.data[0], rx.p->bp.data[1]);
                        requestCompleted(*request);
                        break;
                    case Command::SendingBoardInfo:
                        if (fpRxBoardInfoResponse(rx, *reinterpret_cast<BoardInfo*>(request->returnData.data))) {
                            requestCompleted(*request);
                        }
                        break;
                    case Command::SendingFieldInfo:
                        if (fpRxFieldInfoResponse(rx,
                                                  *reinterpret_cast<FieldEntry*>(request->returnData.data))) {
                            requestCompleted(*request);
                        }
                        break;
                    case Command::SendingFields:
                        updateFields(true, boardPtr, rx.p->bp, rx.dataSize, &request->returnData);
                        fpAssert(request->numFields >= numFields);
                        request->numFields -= numFields;
                        if (request->numFields == 0) {
                            requestCompleted(*request);
                        }
                        break;
                    default:
                        fpAssert(false);  // invalid command
                        break;
                }
            }
            txManager.popRxPacket();
        }
    }

    // Tear down all discovered boards (used by the tests between iterations).
    void deleteFieldProtocol() {
        for (uint32_t i = 0; i < kMaxFpDaemonBoards; i++) {
            if (board[i] != nullptr) {
                deleteBoardTable(i);
            }
        }
    }

    void updateConnectedBoards() {
        uint8_t bitfield[kMaxNumNodes / 8] = {0};

        if (!txManager.creditsInitialised) {
            Request* request = sendGetCreditsRequest();
            waitForRequestToComplete(*request);
            txManager.creditsInitialised = true;
        }

        Request* request = sendGetConnectedNodesRequest(bitfield);
        waitForRequestToComplete(*request);

        bool newConnectedBoardId[kMaxNumNodes] = {false};
        for (uint32_t i = 0; i < kMaxNumNodes; i++) {
            const bool newConnected = (bitfield[i / 8] >> (7 - (i % 8))) & 0x1;
            const bool prevConnected = (connectedBitfield[i / 8] >> (7 - (i % 8))) & 0x1;

            const uint32_t dstNodeId = i;
            if (newConnected != prevConnected) {
                if (newConnected) {
                    newConnectedBoardId[dstNodeId] = true;
                } else {
                    // Remove timed-out board.
                    deleteBoardTable(dstNodeId);
                }
            }
        }
        std::memcpy(connectedBitfield, bitfield, sizeof(bitfield));

        createBoardTables(newConnectedBoardId);
    }

    void getFields(uint8_t dstNodeId, FieldIndex fieldIndex, FieldIndex numFields, uint8_t* data,
                   uint32_t dataSize) {
        sendGetRequest(dstNodeId, Command::GetFields, fieldIndex, numFields, data, dataSize);
        waitForAllRequestsToComplete();
    }

    void setFields(uint8_t dstNodeId, FieldIndex fieldIndex, FieldIndex numFields, uint8_t* data,
                   uint32_t dataSize) {
        waitForFreeRequestAndTxBufferSpace();
        DaemonBoard* boardPtr = findBoard(dstNodeId);
        fpAssert(boardPtr != nullptr);
        Request* request = createNewRequest(dstNodeId, Command::SendingFields, fieldIndex, numFields, data, dataSize);

        // This may require multiple tx packets, so loop through.
        RawData alternativeData{data, 0, dataSize};
        OutgoingResponseState state{};
        state.startField = fieldIndex;
        state.numFields = numFields;
        state.requestId = request->id;
        while (state.numFields > 0) {
            waitForFreeRequestAndTxBufferSpace();
            DaemonFieldPacket txPacket;
            txManager.allocateTxPacket(txPacket);
            txPacket.p->returnTxCredits = 0;
            txPacket.p->returnRxCredits = 0;
            txPacket.p->srcOrDstNodeId = dstNodeId;
            sendFieldsTxPacket(true, *boardPtr, state, txPacket.p->bp, txPacket.dataSize, &alternativeData);
            txManager.submitAllocatedTxPacket(txPacket);
        }
        // The request has completed - it is in the tx buffer and we no longer need to hold it.
        requestCompleted(*request);
    }

    std::array<std::unique_ptr<DaemonBoard>, kMaxFpDaemonBoards> board{};
    std::array<Request, kMaxRequestIds> requests{};
    uint32_t numUsedRequests = 0;
    RequestId nextRequestId = 0;
    uint8_t connectedBitfield[kMaxNumNodes / 8] = {0};
    DaemonTxManager txManager;
    DelayFn delay;

private:
    // ===================================================== //
    // Requests

    uint32_t getNumFreeRequests() const {
        uint32_t count = 0;
        for (const Request& request : requests) {
            if (request.returnData.data == nullptr) {
                count++;
            }
        }
        fpAssert(count == kMaxRequestIds - numUsedRequests);
        return count;
    }

    Request* createNewRequest(uint8_t dstNodeId, Command command, FieldIndex fieldIndex,
                              FieldIndex numFields, uint8_t* returnData, uint32_t dataSize) {
        for (Request& request : requests) {
            if (request.returnData.data == nullptr) {
                request.returnData.data = returnData;
                request.returnData.pos = 0;
                request.returnData.maxSize = dataSize;
                request.dstNodeId = dstNodeId;
                request.id = nextRequestId;
                request.command = command;
                request.fieldIndex = fieldIndex;
                request.numFields = numFields;
                nextRequestId++;
                numUsedRequests++;
                return &request;
            }
        }
        return nullptr;
    }

    Request* findRequest(uint8_t dstNodeId, uint16_t id) {
        for (Request& request : requests) {
            if (request.dstNodeId == dstNodeId && request.id == id) {
                return &request;
            }
        }
        return nullptr;
    }

    void requestCompleted(Request& request) {
        numUsedRequests--;
        request.command = Command::None;
        request.dstNodeId = 0;
        request.returnData.data = nullptr;
    }

    void freeRequestsToNode(uint8_t dstNodeId) {
        for (Request& request : requests) {
            if (request.dstNodeId == dstNodeId && request.returnData.data == nullptr) {
                requestCompleted(request);
            }
        }
    }

    uint32_t getNumOutstandingRequests() const {
        uint32_t count = 0;
        for (const Request& request : requests) {
            if (request.returnData.data != nullptr) {
                count++;
            }
        }
        return count;
    }

    // ===================================================== //
    // Boards

    DaemonBoard* findBoard(uint32_t nodeId) {
        for (auto& b : board) {
            if (b != nullptr && b->nodeId == nodeId) {
                return b.get();
            }
        }
        return nullptr;
    }

    uint32_t findEmptyBoardEntry(uint32_t nodeId) {
        fpAssert(findBoard(nodeId) == nullptr);
        for (uint32_t i = 0; i < kMaxFpDaemonBoards; i++) {
            if (board[i] == nullptr) {
                return i;
            }
        }
        fpAssert(false, "Too many boards");
        return 0;
    }

    void createBoardTables(const bool newConnectedBoardId[kMaxNumNodes]) {
        // Parallelise the requests - partly as a demonstration of how it can be done.

        // Get the board info (in parallel).
        for (uint32_t nodeId = 0; nodeId < kMaxNumNodes; nodeId++) {
            if (newConnectedBoardId[nodeId]) {
                const uint32_t boardIndex = findEmptyBoardEntry(nodeId);
                board[boardIndex] = std::make_unique<DaemonBoard>();
                board[boardIndex]->nodeId = static_cast<uint8_t>(nodeId);
                sendGetBoardInfoRequest(*board[boardIndex]);
            }
        }
        waitForAllRequestsToComplete();

        // Now create the field tables (in parallel).
        for (uint32_t nodeId = 0; nodeId < kMaxNumNodes; nodeId++) {
            if (newConnectedBoardId[nodeId]) {
                DaemonBoard* b = findBoard(nodeId);
                b->allocateFields(b->boardInfo.numFields);
                for (uint32_t i = 0; i < b->boardInfo.numFields; i++) {
                    sendGetFieldInfoRequest(*b, static_cast<FieldIndex>(i), b->fieldStore[i]);
                }
            }
        }
        waitForAllRequestsToComplete();
    }

    void deleteBoardTable(uint32_t boardIndex) {
        if (board[boardIndex] != nullptr) {
            freeRequestsToNode(board[boardIndex]->nodeId);
        }
        board[boardIndex].reset();  // RAII frees the strings, fields and board.
    }

    // ===================================================== //
    // Waiting helpers

    void waitForFreeRequestAndTxBufferSpace() {
        uint64_t iterations = 0;
        while (txManager.txBufferFull() || getNumFreeRequests() == 0) {
            processRx();
            delay();
            iterations++;
            fpAssert(iterations < 1000 * 1000);
        }
    }

    void waitForRequestToComplete(const Request& request) {
        uint64_t iterations = 0;
        while (request.returnData.data != nullptr) {
            processRx();
            delay();
            iterations++;
            fpAssert(iterations < 1000 * 1000);
        }
    }

    void waitForAllRequestsToComplete() {
        uint64_t iterations = 0;
        while (getNumOutstandingRequests() > 0) {
            processRx();
            delay();
            iterations++;
            fpAssert(iterations < 1000 * 1000);
        }
    }

    // ===================================================== //
    // Requests over the wire

    Request* sendGetRequest(uint8_t dstNodeId, Command command, FieldIndex fieldIndex,
                            FieldIndex numFields, uint8_t* returnData, uint32_t returnDataSize) {
        waitForFreeRequestAndTxBufferSpace();
        Request* request = createNewRequest(dstNodeId, command, fieldIndex, numFields, returnData, returnDataSize);
        DaemonFieldPacket txPacket;
        txManager.allocateTxPacket(txPacket);
        txPacket.p->returnRxCredits = 0;
        txPacket.p->returnTxCredits = 0;
        txPacket.p->srcOrDstNodeId = dstNodeId;
        txPacket.p->bp.requestId = request->id;
        txPacket.p->bp.command = static_cast<uint8_t>(command);
        txPacket.p->bp.fieldIndex = fieldIndex;
        txPacket.p->bp.numFields = numFields;
        txManager.submitAllocatedTxPacket(txPacket);
        return request;
    }

    Request* sendGetCreditsRequest() {
        uint8_t data = 0;
        return sendGetRequest(kMasterNodeId, Command::ResetAndGetMaxBufferCredits, 0, 0, &data, 1);
    }

    Request* sendGetConnectedNodesRequest(uint8_t bitfield[kMaxNumNodes / 8]) {
        return sendGetRequest(kMasterNodeId, Command::GetConnectedNodes, 0, 0, bitfield, kMaxNumNodes / 8);
    }

    Request* sendGetBoardInfoRequest(DaemonBoard& b) {
        return sendGetRequest(b.nodeId, Command::GetBoardInfo, 0, 4, reinterpret_cast<uint8_t*>(&b.boardInfo),
                              sizeof(BoardInfo));
    }

    Request* sendGetFieldInfoRequest(DaemonBoard& b, FieldIndex fieldInfoIndex, FieldEntry& entry) {
        return sendGetRequest(b.nodeId, Command::GetFieldInfo, fieldInfoIndex, 1,
                              reinterpret_cast<uint8_t*>(&entry), sizeof(FieldEntry));
    }
};

// ===================================================== //
// Response parser implementations

inline bool fpRxBoardInfoResponse(DaemonFieldPacket& rx, BoardInfo& boardInfo) {
    fpAssert(rx.p->bp.numFields <= 4);
    uint8_t* data = rx.p->bp.data;
    for (uint32_t fi = rx.p->bp.fieldIndex; fi < rx.p->bp.fieldIndex + rx.p->bp.numFields; fi++) {
        switch (fi) {
            case 0:
            case 1: {
                const uint8_t stringLen = data[0];
                std::string value(reinterpret_cast<const char*>(&data[1]), stringLen);
                if (fi == 0) {
                    boardInfo.boardName = std::move(value);
                } else {
                    boardInfo.customName = std::move(value);
                }
                data += stringLen + 1;
                break;
            }
            case 2:
                std::memcpy(&boardInfo.uniqueId, data, 8);
                data += 8;
                break;
            case 3:
                std::memcpy(&boardInfo.numFields, data, sizeof(boardInfo.numFields));
                // We get these back in order, so once the last field arrives the request is complete.
                return true;
        }
    }
    return false;
}

inline bool fpRxFieldInfoResponse(DaemonFieldPacket& rx, FieldEntry& fieldEntry) {
    fpAssert(rx.p->bp.numFields <= 8);
    uint8_t* data = rx.p->bp.data;
    for (uint32_t fi = rx.p->bp.fieldIndex; fi < rx.p->bp.fieldIndex + rx.p->bp.numFields; fi++) {
        switch (fi) {
            case 0:
            case 6: {
                const uint8_t stringLen = data[0];
                std::string value;
                if (stringLen != 0) {
                    value.assign(reinterpret_cast<const char*>(&data[1]), stringLen);
                }
                if (fi == 0) {
                    fieldEntry.name = std::move(value);
                } else {
                    fieldEntry.units = std::move(value);
                }
                data += stringLen + 1;
                if (fi == 6) {
                    return true;
                }
                break;
            }
            case 1:
                // startFieldIndex - ignored for now.
                data += sizeof(FieldIndex);
                break;
            case 2:
                std::memcpy(&fieldEntry.span, data, sizeof(fieldEntry.span));
                data += sizeof(fieldEntry.span);
                break;
            case 3:
                fieldEntry.type = static_cast<FieldDataType>(data[0]);
                data++;
                break;
            case 4:
                std::memcpy(&fieldEntry.size, data, sizeof(fieldEntry.size));
                data += sizeof(fieldEntry.size);
                break;
            case 5:
                std::memcpy(&fieldEntry.flags, data, sizeof(fieldEntry.flags));
                data += sizeof(fieldEntry.flags);
                break;
            default:
                fpAssert(false);  // invalid field index
        }
    }
    return false;
}

#endif  // USE_FP_DAEMON

}  // namespace fp
