// Copyright (c) 2025 Sean Bremner
// Licensed under the MIT License. See LICENSE file for details.
//
// packet.hpp - the on-the-wire format.
//
// THESE STRUCTS DEFINE THE BYTES THAT GO OUT ON SPI. Their layout is a hard
// contract with every other device on the bus (and with real hardware), so the
// member order, sizes and packing here must never change casually. They use
// C-style uint8_t arrays on purpose: the layout must be byte-identical and
// portable, which std::array + __attribute__((packed)) does not reliably give
// across compilers.
//
// What this rewrite changes is *only* the surrounding ergonomics: a scoped
// PacketType enum instead of a bare C enum, and small typed accessor functions
// instead of the old GET_*/SET_* function-like macros.

#pragma once

#include <cstdint>
#include <cstring>

#include "microbus/src/config.hpp"

namespace microbus {

// ---------------------------------------------------------------------------
// Node ids
// ---------------------------------------------------------------------------

// A node's address on the bus. Kept as a plain alias (not a strong type) so it
// can sit directly inside the packed wire structs and be used as an array index.
using NodeId = uint8_t;

inline constexpr NodeId kMasterNode      = 0;
inline constexpr NodeId kFirstNode       = 1;
inline constexpr NodeId kUnallocatedNode = 0xFE;  // a node that has not yet joined
inline constexpr NodeId kInvalidNode     = 0xFF;  // "no node" sentinel

// ---------------------------------------------------------------------------
// Sequence numbers
// ---------------------------------------------------------------------------

// Sequence numbers run 0..254; 255 means "none / invalid / not yet set".
inline constexpr uint8_t kNoSeq = 255;

// ---------------------------------------------------------------------------
// Packet types (low nibble of the first header byte - max 15 values)
// ---------------------------------------------------------------------------

// TODO: consider replacing the empty packet with a dedicated ACK packet that
// can acknowledge several nodes at once. On a single channel this would let the
// master batch acks instead of spending a whole slot per node (see the
// single-channel bandwidth note in master.hpp).
enum class PacketType : uint8_t {
    Null            = 0,
    MasterData      = 1,
    NodeData        = 2,
    MasterEmpty     = 3,
    NodeEmpty       = 4,
    NewNodeRequest  = 5,  // node -> master: "please give me an id"
    NewNodeResponse = 6,  // master -> nodes: id assignments
    MasterReset     = 7,  // master -> all: drop your id and rejoin
};

// ---------------------------------------------------------------------------
// Payloads (the part after the 4-byte common header)
//
// Master and node payloads overlap in a union inside Packet; only one is valid
// per packet, decided by the packet type.
// ---------------------------------------------------------------------------

inline constexpr uint16_t kHeaderSize =
    static_cast<uint16_t>(6 + 2 * kMaxScheduledSlots);
inline constexpr uint16_t kMaxDataSize =
    static_cast<uint16_t>(kPacketSize - kHeaderSize);

// Master -> node. The schedule (nextTxNode*) is read by every node regardless
// of whether the rest of the packet is addressed to it.
struct __attribute__((packed, aligned(2))) MasterPayload {
    // Read first, independently of whether we process the rest of the packet.
    uint8_t nextTxNodeId[kMaxScheduledSlots];
    uint8_t nextTxNodeAckSeqNum[kMaxScheduledSlots];
    // Remaining fields are only relevant when the packet is addressed to us.
    uint8_t dstNodeId;
    uint8_t wirelessDstNodeId;  // reserved for a future wireless bridge
    uint8_t data[kMaxDataSize];
};

// Node -> master.
struct __attribute__((packed, aligned(2))) NodePayload {
    uint8_t ackSeqNum;
    uint8_t srcNodeId;
    uint8_t srcWirelessNodeId;  // reserved
    uint8_t bufferLevel;        // how many packets this node still wants to send
    uint8_t spare[2 * kMaxScheduledSlots - 2];
    uint8_t data[kMaxDataSize];
};

// A full packet: 4-byte common header + one payload.
struct __attribute__((packed, aligned(2))) Packet {
    uint8_t protocolVersionAndPacketType;  // version<<4 | type
    uint8_t txSeqNum;
    uint8_t dataSize1;  // size, high byte
    uint8_t dataSize2;  // size, low byte
    union {
        MasterPayload master;
        NodePayload node;
    };
};

// The full wire packet must be exactly kPacketSize bytes.
static_assert(sizeof(Packet) == kPacketSize, "Packet size should match constant");

// PacketStub - a deliberately under-sized stand-in for an empty packet.
//
// When a device "has nothing to send" it still must transmit something every
// slot. Rather than zero a full kPacketSize payload each time, we fill in only
// the header fields here and hand the DMA this small buffer; the DMA reads
// kPacketSize bytes, so it harmlessly over-reads trailing bytes that the
// receiver ignores (data size is 0). The leading layout MUST match Packet's so
// the accessors below work on either.
struct __attribute__((packed, aligned(2))) PacketStub {
    uint8_t protocolVersionAndPacketType;
    uint8_t txSeqNum;
    uint8_t dataSize1;
    uint8_t dataSize2;
    union {
        struct {
            uint8_t nextTxNodeId[kMaxScheduledSlots];
            uint8_t nextTxNodeAckSeqNum[kMaxScheduledSlots];
            uint8_t dstNodeId;
            uint8_t spare;
        } master;
        struct {
            uint8_t ackSeqNum;
            uint8_t srcNodeId;
            uint8_t bufferLevel;
            uint8_t spare[7];
        } node;
    };
};

// ---------------------------------------------------------------------------
// Typed header accessors (replace the old GET_*/SET_* macros)
//
// Templated on the packet-like type so they work for both Packet and
// PacketStub, which share the same first four header bytes.
// ---------------------------------------------------------------------------

template <typename P>
constexpr PacketType packetType(const P* p) noexcept {
    return static_cast<PacketType>(p->protocolVersionAndPacketType & 0x0F);
}

template <typename P>
constexpr uint8_t protocolVersion(const P* p) noexcept {
    return static_cast<uint8_t>((p->protocolVersionAndPacketType >> 4) & 0x0F);
}

template <typename P>
constexpr uint16_t dataSize(const P* p) noexcept {
    return static_cast<uint16_t>((p->dataSize1 << 8) | p->dataSize2);
}

template <typename P>
constexpr void setVersionAndType(P* p, PacketType type) noexcept {
    p->protocolVersionAndPacketType = static_cast<uint8_t>(
        (kProtocolVersion << 4) | (static_cast<uint8_t>(type) & 0x0F));
}

template <typename P>
constexpr void setDataSize(P* p, uint16_t size) noexcept {
    p->dataSize1 = static_cast<uint8_t>(size >> 8);
    p->dataSize2 = static_cast<uint8_t>(size & 0xFF);
}

// ---------------------------------------------------------------------------
// A Packet plus an "in use" flag, the unit of storage in the tx/rx pools.
// ---------------------------------------------------------------------------

struct PacketEntry {
    bool inUse;
    Packet packet;
};

// ---------------------------------------------------------------------------
// Join handshake payload entries (serialised into packet data)
// ---------------------------------------------------------------------------

// uint64_t uniqueId + uint8_t nodeId, packed.
inline constexpr uint8_t kNewNodeEntrySize = 9;

struct NewNodeEntry {
    uint64_t uniqueId;
    NodeId nodeId;
};

inline void writeNewNodeEntry(uint8_t* dst, const NewNodeEntry& e) noexcept {
    std::memcpy(dst, &e.uniqueId, 8);
    dst[8] = e.nodeId;
}

inline NewNodeEntry readNewNodeEntry(const uint8_t* src) noexcept {
    NewNodeEntry e{};
    std::memcpy(&e.uniqueId, src, 8);
    e.nodeId = src[8];
    return e;
}

// ---------------------------------------------------------------------------
// Debug pretty-printer (defined in src/microbus.cpp).
// ---------------------------------------------------------------------------

void printPacket(const Packet* packet, bool isMaster, NodeId nodeId, bool isTx,
                 uint8_t numScheduled) noexcept;

}  // namespace microbus
