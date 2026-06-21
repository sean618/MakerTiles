// Copyright (c) 2025 Sean Bremner
// Licensed under the MIT License. See LICENSE file for details.
//
// config.hpp - all the compile-time knobs for the protocol in one place.
//
// Everything here is `constexpr`, so the values are baked into the firmware and
// cost no RAM. Change them to trade off latency, throughput, buffer sizes and
// the maximum number of nodes against the resources of your microcontroller.

#pragma once

#include <cstdint>

namespace microbus {

// Bring the fixed-width integer types into this namespace so the rest of the
// library can use them unqualified (uint8_t rather than std::uint8_t). Only the
// std:: names are guaranteed by <cstdint>, so we alias from there.
using std::int32_t;
using std::uint16_t;
using std::uint32_t;
using std::uint64_t;
using std::uint8_t;



// inline constexpr bool kDualChannel = true;

// Should be based on 
// How many future slots the master publishes in every packet. Receivers use
// this look-ahead to know, in advance, which slot is theirs to transmit in.
inline constexpr uint8_t kMaxScheduledSlots = 4;


// ---------------------------------------------------------------------------
// Network size
// ---------------------------------------------------------------------------

// Maximum number of nodes on the bus, including the master. Must be < 255 so
// that 0xFE / 0xFF stay free as the "unallocated" and "invalid" sentinels.
inline constexpr uint8_t kMaxNodes = 64;

// ---------------------------------------------------------------------------
// Slot timing
// ---------------------------------------------------------------------------

// Every packet on the wire is exactly this many bytes (header + payload). A
// fixed size keeps the SPI/DMA setup trivial and makes slot timing predictable.
//   At 4.5 MHz this is ~355 us per packet, at 9 MHz ~177 us.
inline constexpr uint16_t kPacketSize = 192;

// Wall-clock duration of one slot, derived from the packet size assuming the
// reference 4.5 MHz line rate: 8 bits/byte * size * 10/45 (i.e. /4.5 MHz, in us).
inline constexpr uint32_t kSlotTimeUs = (8u * kPacketSize * 10u) / 45u;

// ---------------------------------------------------------------------------
// Reliability (sliding-window retransmission)
// ---------------------------------------------------------------------------

// How many packets a sender may have in flight (unacked) at once, per peer.
inline constexpr uint8_t kSlidingWindowSize = 4;

// After the window edge is reached, how many acks we wait for before assuming
// loss and restarting the window from its start.
inline constexpr uint8_t kAcksBeforeWindowRestart = 2;

// ---------------------------------------------------------------------------
// Scheduling (master decides who transmits in each upcoming slot)
// ---------------------------------------------------------------------------


// Must match the tx queue depth: a node can't have more outstanding acks than
// it has window slots.
inline constexpr uint8_t kMaxSlotsBetweenAcks = 4;

// Upper bound on how long an active node can go without being given a slot.
inline constexpr uint8_t kMaxSlotsBetweenServicing = 6;

// Gap (in slots) between "unallocated" slots offered to nodes that want to
// join. Starts tight so joins are fast, then relaxes once the bus is quiet.
inline constexpr uint8_t kMinSlotsBetweenJoin = 2;
inline constexpr uint8_t kMaxSlotsBetweenJoin = 80;
inline constexpr uint8_t kSlotsBeforeJoinGapDoubles = 128;

// ---------------------------------------------------------------------------
// Membership (joining / leaving)
// ---------------------------------------------------------------------------

// Most nodes the master will hold in its "allocated but not yet fully joined"
// table at once.
inline constexpr uint8_t kMaxPendingJoins = 10;

// Protocol version carried in every header's high nibble.
inline constexpr uint8_t kProtocolVersion = 1;

}  // namespace microbus
