// Copyright (c) 2025 Sean Bremner
// Licensed under the MIT License. See LICENSE file for details.
//
// microbus.hpp - umbrella header.
//
// Include this to pull in the whole MicroBus API. Individual headers can also
// be included directly if you only need part of it.
//
// MicroBus is a master/multi-node protocol that runs over a shared SPI-style
// link between microcontrollers. It is built for speed and very low RAM: fixed
// 192-byte packets, a time-slotted schedule broadcast by the master in every
// packet, and a small per-node sliding window that gives reliable, in-order
// delivery without dynamic allocation.

#pragma once

#include "microbus/src/config.hpp"       // compile-time tunables
#include "microbus/src/diagnostics.hpp"  // logging + assertions
#include "microbus/src/util.hpp"         // sequence-number math, RNG
#include "microbus/src/ring_buffer.hpp"  // fixed-capacity ring buffer
#include "microbus/src/packet.hpp"       // wire format + accessors
#include "microbus/src/stats.hpp"        // diagnostic counters
#include "microbus/src/node_queue.hpp"   // small set-of-node-ids helper
#include "microbus/src/tx_engine.hpp"    // reliable per-destination tx
#include "microbus/src/rx_queue.hpp"     // in-order rx delivery
#include "microbus/src/network.hpp"      // join/leave membership
#include "microbus/src/scheduler.hpp"    // master slot scheduling
#include "microbus/src/master_tx.hpp"    // master-side tx policy
#include "microbus/src/node.hpp"         // Node device
#include "microbus/src/master.hpp"       // Master device
