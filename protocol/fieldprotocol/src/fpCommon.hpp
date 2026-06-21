/*
 * Copyright © 2024 - 2024 Sean Bremner. All rights reserved.>>
 *
 * Idiomatic C++ port of the original C "Field Protocol" sources.
 */

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <source_location>
#include <string>
#include <vector>

#ifndef USE_MOCK_MICROBUS
    #include "../../microbus/src/microbus.hpp"
#endif

// Bring the fixed-width integer types (and size_t) into scope so callers can
// write `uint8_t` instead of `std::uint8_t`. Done at global scope here so every
// translation unit that includes this header (the field protocol sources and
// the tests) picks them up.
using std::int8_t;
using std::int16_t;
using std::int32_t;
using std::int64_t;
using std::uint8_t;
using std::uint16_t;
using std::uint32_t;
using std::uint64_t;
using std::size_t;

namespace fp {

inline constexpr uint8_t kFieldProtocolVersion = 0;

// These come from the microbus layers but we have a mock of those layers too
#ifndef USE_MOCK_MICROBUS
    // The field protocol packet fits in the data section of a microbus packet.
    // The rewritten microbus uses a single shared data-section size for both
    // master and node payloads (microbus::kMaxDataSize).
    inline constexpr uint16_t kMaxBusPacketSize  = microbus::kMaxDataSize;
    inline constexpr uint8_t  kMaxNumNodes       = microbus::kMaxNodes;
    inline constexpr uint8_t  kMasterNodeId      = microbus::kMasterNode;
    inline constexpr uint8_t  kInvalidNodeId     = microbus::kInvalidNode;
    inline constexpr uint8_t  kUnallocatedNodeId = microbus::kUnallocatedNode;
#else
    inline constexpr uint16_t kMaxBusPacketSize  = (200 - 8);
    inline constexpr uint8_t  kMaxNumNodes       = 64;
    inline constexpr uint8_t  kMasterNodeId      = 0;
    inline constexpr uint8_t  kInvalidNodeId     = 0xFF;
    inline constexpr uint8_t  kUnallocatedNodeId = 0xFE;
#endif

// ==================================== //
// Logging
//
// The original code used a fprintf macro guarded by FP_LOGGING. We keep an
// equivalent that compiles to nothing when logging is disabled, but route it
// through a variadic function template so the format string is type checked.

inline constexpr int kMicrobusLogging = 0;
inline constexpr int kFpLogging = 0;

inline std::FILE* logfile = nullptr;

template <typename... Args>
inline void fpPrintf([[maybe_unused]] const char* fmt, [[maybe_unused]] Args... args) {
    if constexpr (kFpLogging > 0) {
        std::fprintf(logfile, "FP:");
        std::fprintf(logfile, fmt, args...);
    }
}

// ==================================== //
// Assertions
//
// FieldProtocol provides its own (weak) assert handler. On a PC/host build it
// prints the failing assertion and aborts so test failures surface immediately;
// on an MCU it spins forever, matching the original embedded behaviour (there is
// no stderr to print to and aborting would just reset the part).
//
// "Host" is detected via the standard __STDC_HOSTED__ macro (1 on a hosted
// implementation such as a PC, 0 on the freestanding toolchains used for MCUs).
// Define FP_HOST_ASSERT to 0 or 1 to override the auto-detection explicitly.

#ifndef FP_HOST_ASSERT
#  if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1
#    define FP_HOST_ASSERT 1
#  else
#    define FP_HOST_ASSERT 0
#  endif
#endif

#define FP_STRINGIFY(x) #x
#define FP_TOSTRING(x) FP_STRINGIFY(x)
#define fpAssert(_pred_, ...)      fpAssertLL(_pred_, __FILE__ ": " FP_TOSTRING(__LINE__) " - " __VA_ARGS__)
#define fpAssertLL(_pred_, _msg_)  if (!(_pred_)) ::fp::assertMessage(_msg_, sizeof(_msg_))

inline void assertMessage([[maybe_unused]] const char* msg, size_t /*msgLen*/) {
    if constexpr (kFpLogging > 0) {
        std::fflush(logfile);
    }
#if FP_HOST_ASSERT
    // PC build: report and abort so the failure is visible and the process exits.
    std::fprintf(stderr, "FP ASSERT FAILED: %s\n", msg);
    std::fflush(stderr);
    std::abort();
#else
    // MCU build: hang so a debugger can inspect the stalled state.
    while (true) {
    }
#endif
}

// ==================================== //
// Field table definitions

using RequestId = uint8_t;
using FieldIndex = uint16_t;

// Field/board name strings.
//
// On a hosted build (the PC daemon + tests) these are owned std::strings: the
// daemon discovers remote boards at runtime and must store the names it
// receives. On a freestanding build (the MCU node/master) the field tables are
// static descriptors whose names are compile-time string literals, so a plain
// `const char*` is used instead - this keeps the names in flash and, crucially,
// avoids dragging in the heap allocator and the libstdc++ std::string runtime
// (which alone cost ~10 KB of flash/RAM, overflowing the 32K/8K node parts).
//
// Both forms support the read operations the protocol needs (see fpStrLen /
// fpStrData below), so the serialisation code is identical for both.
//
// The discriminator is __arm__: defined for every MCU target (arm-none-eabi)
// and never on the host PC build. Note __STDC_HOSTED__ is unreliable here -
// arm-none-eabi-g++ reports it as 1 unless -ffreestanding is passed. Define
// FP_OWNED_STRINGS to 0/1 to override explicitly.
#ifndef FP_OWNED_STRINGS
#  if defined(__arm__)
#    define FP_OWNED_STRINGS 0
#  else
#    define FP_OWNED_STRINGS 1
#  endif
#endif

#if FP_OWNED_STRINGS
    using FpString = std::string;
#else
    using FpString = const char*;
#endif

inline size_t fpStrLen(const std::string& s) { return s.size(); }
inline const char* fpStrData(const std::string& s) { return s.data(); }
inline size_t fpStrLen(const char* s) { return s ? std::strlen(s) : 0; }
inline const char* fpStrData(const char* s) { return s; }

enum class FieldDataType : uint8_t {
    Null       = 0,
    Raw        = 1,
    Enum       = 2,
    Boolean    = 3,
    Uint       = 4,
    Int        = 5,
    Float      = 6,
    Time       = 7,
    Utf8Char   = 8,
    Utf8String = 9,
    Dictionary = 10,
};

inline constexpr uint8_t kMaxFieldSize = 8;  // 8 bytes for uint64_t
inline constexpr uint8_t kFieldDataSizeVariableSize = 0;

// Field access flags. Scoped so callers write `Flags::Gettable | Flags::Settable`
// instead of bare ints, while still serialising to a single byte on the wire.
enum class FieldFlags : uint8_t {
    None      = 0,
    Gettable  = 1 << 0,
    Settable  = 1 << 1,
    Joined    = 1 << 2,
    Streaming = 1 << 3,
};

constexpr FieldFlags operator|(FieldFlags a, FieldFlags b) {
    return static_cast<FieldFlags>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}
constexpr FieldFlags operator&(FieldFlags a, FieldFlags b) {
    return static_cast<FieldFlags>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
}
constexpr bool isGettable(FieldFlags f) { return (f & FieldFlags::Gettable) != FieldFlags::None; }
constexpr bool isSettable(FieldFlags f) { return (f & FieldFlags::Settable) != FieldFlags::None; }
constexpr bool isJoined(FieldFlags f)   { return (f & FieldFlags::Joined)   != FieldFlags::None; }

struct FieldEntry;  // forward declaration

// Field accessor callbacks. These remain function pointers because field
// tables are static descriptor tables: a lightweight, trivially-copyable POD
// representation is exactly what is wanted here.
//
// setFieldFn returns true on success.
using SetFieldFn = bool (*)(FieldEntry* field, FieldIndex fieldOffset, FieldIndex numFields,
                            uint8_t* data);
using GetFieldFn = uint8_t* (*)(FieldEntry* field, FieldIndex fieldOffset, FieldIndex numFields);

struct FieldEntry {
    void* ptr = nullptr;                 // backing storage for the field value
    FpString name{};                     // field name (literal on MCU, std::string on host)
    FieldIndex span = 0;                 // number of array elements
    FieldDataType type = FieldDataType::Null;
    uint8_t size = 0;               // bytes per element
    FieldFlags flags = FieldFlags::None;
    SetFieldFn setFieldFn = nullptr;
    GetFieldFn getFieldFn = nullptr;
    FpString units{};
};

struct FieldTable {
    FieldEntry* fields = nullptr;
    FieldIndex numFields = 0;
};

struct BoardInfo {
    FpString boardName{};
    FpString customName{};
    uint64_t uniqueId = 0;
    FieldIndex numFields = 0;
};

// ==================================== //
// Field protocol command set

enum class Command : uint8_t {
    None = 0,
    GetBoardInfo,
    SetBoardInfo,
    SendingBoardInfo,
    GetFieldInfo,
    SendingFieldInfo,
    GetFields,
    SendingFields,
    ResetAndGetMaxBufferCredits,
    SendingMaxBufferCredits,
    GetConnectedNodes,
    SendingConnectedNodes,
    ReturningCredits,
};

const char* commandName(Command command);

// ==================================== //
// Packet definitions
//
// These structs describe the exact byte layout on the wire, so they remain
// packed standard-layout aggregates - the idiomatic representation for a
// serialisation format.

// This bus packet has to fit in the data part of the microbus packet.
inline constexpr uint8_t kFpBusHeaderBytes = 6;
inline constexpr uint16_t kMaxFpPacketDataSize = kMaxBusPacketSize - kFpBusHeaderBytes;

#pragma pack(push, 1)
struct RawBusFieldPacket {
    RequestId requestId;     // 1 byte
    uint8_t command;    // 1 byte (wire form of Command)
    FieldIndex numFields;    // 2 bytes
    FieldIndex fieldIndex;   // 2 bytes
    uint8_t data[kMaxFpPacketDataSize];
};

// The daemon packet goes over USB and therefore can carry more than the bus packet.
inline constexpr uint8_t kFpDaemonExtraHeaderBytes = 5;
struct RawDaemonFieldPacket {
    uint16_t packetSize;
    uint8_t returnRxCredits;
    uint8_t returnTxCredits;
    uint8_t srcOrDstNodeId;
    RawBusFieldPacket bp;
};
#pragma pack(pop)

inline constexpr uint8_t kFpDaemonHeaderBytes = kFpBusHeaderBytes + kFpDaemonExtraHeaderBytes;
inline constexpr uint16_t kMaxDaemonPacketSize = kMaxFpPacketDataSize + kFpDaemonHeaderBytes;

struct BusFieldPacket {
    RawBusFieldPacket* p = nullptr;
    uint16_t dataSize = 0;
};

struct DaemonFieldPacket {
    RawDaemonFieldPacket* p = nullptr;
    uint16_t dataSize = 0;
};

// A simple cursor over a caller-owned byte buffer.
struct RawData {
    uint8_t* data = nullptr;
    uint32_t pos = 0;
    uint32_t maxSize = 0;
};

struct FpInterfaceStats {
    uint64_t rxBufferFull = 0;
    uint64_t txBufferFull = 0;
};

// ==================================== //
// Transport interface
//
// The original C code modelled this as a struct of four function pointers plus
// an opaque `void* state`. In C++ this is naturally an abstract base class: the
// concrete transport (mock, microbus, direct-call, ...) derives from it and
// holds its own state as member data.

class FpInterface {
public:
    virtual ~FpInterface() = default;

    // Reserve space for a tx packet and return a pointer to it, or nullptr if
    // the tx buffer is full.
    virtual uint8_t* allocateTxPacket() = 0;

    // Commit the previously allocated tx packet to the given destination node.
    virtual void submitTxPacket(uint8_t dstNodeId, uint16_t packetSize) = 0;

    // Return a pointer to the next rx packet (without removing it), or nullptr.
    virtual uint8_t* peekRxPacket(uint16_t& numBytes, uint8_t& srcNodeId) = 0;

    // Remove the most recently peeked rx packet. Returns true on success.
    virtual bool popRxPacket() = 0;

    // The node id associated with this transport endpoint. Node-bus transports
    // override this; transports that have no node identity keep the default.
    virtual uint8_t getNodeId() const { return kInvalidNodeId; }

    bool isDaemon = false;
    bool daemonMasterLink = false;
    FpInterfaceStats stats{};
};

// For nodes this is our board state; for the daemon this is the destination
// node's state.
struct FpBoard {
    // If daemon, this refers to the dst node id; if node, our own node id.
    uint8_t nodeId = 0;
    BoardInfo boardInfo{};
    FieldTable** tables = nullptr;
    uint8_t numTables = 0;
};

struct OutgoingResponseState {
    Command command = Command::None;
    RequestId requestId = 0;
    FieldIndex startField = 0;
    FieldIndex numFields = 0;
};

// ==================================== //
// Circular buffer index arithmetic (shared by the daemon tx manager and the
// test transports).

constexpr uint32_t incrAndWrap(uint32_t val, uint32_t incr, uint32_t max) {
    return (val >= max - incr) ? (incr - (max - val)) : (val + incr);
}
constexpr uint32_t decrAndWrap(uint32_t val, uint32_t decr, uint32_t max) {
    return (val < decr) ? (max - decr + val) : (val - decr);
}
constexpr bool circularBufferFull(uint32_t head, uint32_t tail, uint32_t size) {
    return head == incrAndWrap(tail, 1, size);
}
constexpr bool circularBufferEmpty(uint32_t head, uint32_t tail, uint32_t /*size*/) {
    return head == tail;
}
constexpr uint32_t circularBufferLength(uint32_t head, uint32_t tail, uint32_t size) {
    return decrAndWrap(tail, head, size);
}

// ==================================== //
// Implementations

inline const char* commandName(Command command) {
    switch (command) {
        case Command::None:                        return "NULL_COMMAND";
        case Command::GetBoardInfo:                return "GET_BOARD_INFO";
        case Command::SetBoardInfo:                return "SET_BOARD_INFO";
        case Command::SendingBoardInfo:            return "SENDING_BOARD_INFO";
        case Command::GetFieldInfo:                return "GET_FIELD_INFO";
        case Command::SendingFieldInfo:            return "SENDING_FIELD_INFO";
        case Command::GetFields:                   return "GET_FIELDS";
        case Command::SendingFields:               return "SENDING_FIELDS";
        case Command::ResetAndGetMaxBufferCredits: return "RESET_AND_GET_MAX_BUFFER_CREDITS";
        case Command::SendingMaxBufferCredits:     return "SENDING_MAX_BUFFER_CREDITS";
        case Command::GetConnectedNodes:           return "GET_CONNECTED_NODES";
        case Command::SendingConnectedNodes:       return "SENDING_CONNECTED_NODES";
        case Command::ReturningCredits:            return "RETURNING_CREDITS";
    }
    return "UNKNOWN";
}



inline void writeNextPacketField(RawBusFieldPacket& txPacket, uint16_t& dataSize,
                                const void* fieldData, uint32_t fieldSize) {
    std::memcpy(&txPacket.data[dataSize], fieldData, fieldSize);
    dataSize += fieldSize;
}

// Accepts either an owned std::string (host) or a const char* literal (MCU);
// fpStrLen/fpStrData abstract the difference.
inline void writeSendingRequestForString(RawBusFieldPacket& txPacket, uint16_t& dataSize,
                                        const FpString& string) {
    const uint8_t numBytes = static_cast<uint8_t>(fpStrLen(string));
    // First byte is the size of the string...
    txPacket.data[dataSize] = numBytes;
    dataSize++;
    // ...then the string itself.
    writeNextPacketField(txPacket, dataSize, fpStrData(string), numBytes);
}

inline void txBoardInfoResponse(BoardInfo &boardInfo, OutgoingResponseState& state, RawBusFieldPacket& txPacket, uint16_t& dataSize) {
    fpAssert(state.numFields == 4, "");
    txPacket.requestId = state.requestId;
    txPacket.command = static_cast<uint8_t>(Command::SendingBoardInfo);
    txPacket.fieldIndex = 0;
    txPacket.numFields = 4;
    dataSize = 0;
    writeSendingRequestForString(txPacket, dataSize, boardInfo.boardName);
    writeSendingRequestForString(txPacket, dataSize, boardInfo.customName);
    writeNextPacketField(txPacket, dataSize, &boardInfo.uniqueId, sizeof(boardInfo.uniqueId));
    writeNextPacketField(txPacket, dataSize, &boardInfo.numFields, sizeof(boardInfo.numFields));
    fpAssert(dataSize < kMaxFpPacketDataSize, "");
    state = OutgoingResponseState{};
}

inline void txFieldInfoResponse(FpBoard& board, OutgoingResponseState& state, RawBusFieldPacket& txPacket, uint16_t& dataSize) {
    fpAssert(state.numFields == 1, "");
    txPacket.requestId = state.requestId;
    txPacket.command = static_cast<uint8_t>(Command::SendingFieldInfo);
    txPacket.fieldIndex = 0;
    txPacket.numFields = 7;
    dataSize = 0;
    // The start field is calculated dynamically.
    FieldIndex startFieldIndex = 0;
    FieldIndex currStartField = 0;
    FieldEntry* fieldEntry = &board.tables[0]->fields[0];
    for (FieldIndex t = 0; t < board.numTables; t++) {
        const FieldTable* table = board.tables[t];
        for (FieldIndex f = 0; f < table->numFields; f++) {
            if (currStartField == state.startField) {
                fieldEntry = &table->fields[f];
            } else if (currStartField < state.startField) {
                startFieldIndex += table->fields[f].span;
            }
            currStartField++;
        }
    }
    writeSendingRequestForString(txPacket, dataSize, fieldEntry->name);
    writeNextPacketField(txPacket, dataSize, &startFieldIndex, sizeof(startFieldIndex));
    writeNextPacketField(txPacket, dataSize, &fieldEntry->span, sizeof(fieldEntry->span));
    writeNextPacketField(txPacket, dataSize, &fieldEntry->type, 1);
    writeNextPacketField(txPacket, dataSize, &fieldEntry->size, sizeof(fieldEntry->size));
    writeNextPacketField(txPacket, dataSize, &fieldEntry->flags, sizeof(fieldEntry->flags));
    writeSendingRequestForString(txPacket, dataSize, fieldEntry->units);
    fpAssert(dataSize < kMaxFpPacketDataSize, "Tx packet size exceeded");
    state = OutgoingResponseState{};
}

// Work out how many fields we can access from an array field and where the
// starting pointer is.
inline void calcNumArrayFieldsToAccess(const FieldEntry& field, FieldIndex fieldIndex,
                                    FieldIndex endFieldIndex, FieldIndex fieldEntryStartFieldIndex,
                                    FieldIndex& numFields, void*& fieldPtr) {
    const FieldIndex fieldOffset = fieldIndex - fieldEntryStartFieldIndex;
    const FieldIndex remainingFields = endFieldIndex - fieldIndex;
    const FieldIndex remainingArrayFields = fieldEntryStartFieldIndex + field.span - fieldIndex;
    numFields = std::min(remainingFields, remainingArrayFields);
    fieldPtr = static_cast<uint8_t*>(field.ptr) + (field.size * fieldOffset);
}

inline FieldIndex fillTxPacket(bool isDaemon, FpBoard& board, OutgoingResponseState& state, RawBusFieldPacket& txPacket, 
                            uint16_t& dataSize, RawData* alternativeData) {
    const FieldIndex endFieldIndex = state.numFields + state.startField;

    // Maintain some state whilst we loop through field entries.
    FieldIndex fieldIndex = state.startField;
    FieldIndex fieldStartFieldIndex = 0;

    // Go through each table entry until we've set/get the specified num fields.
    for (uint8_t t = 0; t < board.numTables; t++) {
        const FieldTable* table = board.tables[t];
        for (FieldIndex fieldEntryIndex = 0; fieldEntryIndex < table->numFields; fieldEntryIndex++) {
            FieldEntry& field = table->fields[fieldEntryIndex];
            const FieldIndex nextStart = fieldStartFieldIndex + field.span;

            if (fieldIndex >= fieldStartFieldIndex && fieldIndex < nextStart) {
                fpAssert(field.size > 0, "");  // variable field length not supported

                FieldIndex numArrayFields = 1;
                void* fieldPtr = field.ptr;
                uint32_t numBytes = field.size;
                bool packetFull = false;
                const uint32_t packetBytesRemaining = kMaxFpPacketDataSize - dataSize - 1;

                // Not enough space.
                if (packetBytesRemaining < numBytes) {
                    return fieldIndex;
                }

                // If the field is an array then we set as many as possible.
                if (field.span > 1) {
                    calcNumArrayFieldsToAccess(field, fieldIndex, endFieldIndex, fieldStartFieldIndex,
                                            numArrayFields, fieldPtr);
                    numBytes = numArrayFields * field.size;

                    // Adjust how many fields we are sending based on the remaining space.
                    if (packetBytesRemaining < numBytes) {
                        packetFull = true;
                        numArrayFields = packetBytesRemaining / field.size;
                        numBytes = numArrayFields * field.size;
                    }
                }

                // If a bespoke data pointer is supplied then use that instead of the table field ptr.
                if (alternativeData != nullptr) {
                    fieldPtr = &alternativeData->data[alternativeData->pos];
                    alternativeData->pos += numBytes;
                } else if (field.getFieldFn != nullptr) {
                    fieldPtr = field.getFieldFn(&field, fieldIndex - fieldStartFieldIndex, numArrayFields);
                }

                if (isGettable(field.flags) || isDaemon) {
                    // We need to fill in the tx packet with our field data.
                    std::memcpy(&txPacket.data[dataSize], fieldPtr, numBytes);
                } else {
                    std::memset(&txPacket.data[dataSize], 0, numBytes);
                }

                dataSize += numBytes;
                fieldIndex += numArrayFields;

                if (packetFull || fieldIndex >= endFieldIndex) {
                    return fieldIndex;
                }
            }

            fieldStartFieldIndex = nextStart;
        }
    }
    return fieldIndex;
}


inline bool updateFields(bool isDaemon, FpBoard * board, RawBusFieldPacket& rxPacket, uint16_t dataSize, RawData* alternativeData) {
    const FieldIndex endFieldIndex = rxPacket.numFields + rxPacket.fieldIndex;

    // Maintain some state whilst we loop through field entries.
    uint32_t dataPos = 0;
    FieldIndex fieldIndex = rxPacket.fieldIndex;
    FieldIndex fieldStartFieldIndex = 0;

    bool done = false;

    // Go through each table entry until we've set the specified num fields.
    for (uint8_t t = 0; t < board->numTables && !done; t++) {
        const FieldTable* table = board->tables[t];
        for (FieldIndex fieldEntryIndex = 0; fieldEntryIndex < table->numFields; fieldEntryIndex++) {
            FieldEntry& field = table->fields[fieldEntryIndex];
            const FieldIndex nextStart = fieldStartFieldIndex + field.span;

            if (fieldIndex >= fieldStartFieldIndex && fieldIndex < nextStart) {
                fpAssert(field.size > 0);  // variable field length not supported

                FieldIndex numArrayFields = 1;
                void* fieldPtr = field.ptr;
                uint32_t numBytes = field.size;

                // If the field is an array then we set as many as possible.
                if (field.span > 1) {
                    calcNumArrayFieldsToAccess(field, fieldIndex, endFieldIndex, fieldStartFieldIndex,
                                            numArrayFields, fieldPtr);
                    numBytes = numArrayFields * field.size;
                }

                // If a bespoke data pointer is supplied then use that instead of the table field ptr.
                if (alternativeData != nullptr) {
                    fieldPtr = &alternativeData->data[alternativeData->pos];
                    fpAssert(numBytes <= alternativeData->maxSize - alternativeData->pos,
                            "alternate data ptr exceeded");
                    alternativeData->pos += numBytes;
                }
                if (dataPos + numBytes > dataSize) {
                    // The daemon sent us too much data for the num fields specified. Bail out
                    // gracefully. TODO: surface an explicit error here.
                    done = true;
                    break;
                }

                uint8_t* data = &rxPacket.data[dataPos];

                if (isSettable(field.flags) || isDaemon) {
                    if (field.setFieldFn != nullptr) {
                        const bool res =
                            field.setFieldFn(&field, fieldIndex - fieldStartFieldIndex, numArrayFields, data);
                        if (!res) {
                            return false;
                        }
                    } else {
                        std::memcpy(fieldPtr, data, numBytes);
                    }
                    fpPrintf("%s %u: UPDATING_FIELDS, field:%u, numFields:%u, requestId:%u\n",
                            isDaemon ? "Daemon" : "Node", board->nodeId, fieldIndex, numArrayFields,
                            rxPacket.requestId);
                } else {
                    fpPrintf("%s %u: Ignoring UPDATING_FIELDS, field:%u, numFields:%u, requestId:%u\n",
                            isDaemon ? "Daemon" : "Node", board->nodeId, fieldIndex, numArrayFields,
                            rxPacket.requestId);
                }

                dataPos += numBytes;
                fieldIndex += numArrayFields;

                if (fieldIndex >= endFieldIndex) {
                    done = true;
                    break;
                }
            }

            fieldStartFieldIndex = nextStart;
        }
    }

    if (dataPos != dataSize) {
        fpAssert(dataPos < dataSize, "Set more data than packet size");
        // The daemon sent us too little data for the num fields specified.
        // TODO: surface an explicit error here.
        return true;
    }
    return true;
}

inline void sendFieldsTxPacket(bool isDaemon, FpBoard& board, OutgoingResponseState& state, RawBusFieldPacket& txPacket,
                    uint16_t& dataSize, RawData* alternativeData) {
    dataSize = 0;
    const FieldIndex fieldIndex = fillTxPacket(isDaemon, board, state, txPacket, dataSize, alternativeData);

    txPacket.requestId = state.requestId;
    txPacket.command = static_cast<uint8_t>(Command::SendingFields);
    txPacket.fieldIndex = state.startField;
    txPacket.numFields = fieldIndex - state.startField;

    if (state.numFields == txPacket.numFields) {
        state = OutgoingResponseState{};
    } else {
        state.startField = fieldIndex;
        state.numFields -= txPacket.numFields;
    }

    fpAssert(dataSize < kMaxFpPacketDataSize, "Data overflowed packet");
    if (state.numFields > 0 && txPacket.numFields == 0) {
        fpAssert(false, "Zero fields added to packet");
        state = OutgoingResponseState{};
    }
}


}  // namespace fp
