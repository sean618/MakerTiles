/*
 * Copyright © 2024 - 2024 Sean Bremner. All rights reserved.>>
 */

#pragma once

#include <cstdint>
#include <optional>

#include "../src/fpCommon.hpp"
#include "../src/fpMaster.hpp"
#include "../src/fpNode.hpp"
#include "microbusMock.hpp"
#include "realMicrobus.hpp"
#include "testCommon.hpp"
#include "usbMock.hpp"
#ifdef USE_FP_DAEMON
#include "../src/fpDaemon.hpp"
#endif

// ============================================================== //
// Network simulation
//
// A NetworkSim owns one simulated transport fabric (the master<->node bus plus
// the daemon<->master USB link) and exposes the fp::FpInterface objects the
// protocol code binds to, the credit limits, and the connected-node / tx-depth
// callbacks. step() advances the fabric by one slot.
//
// This replaces the old eTestFramework enum that was switched on in three
// separate places (getFrameworkCalls, createMasterNodeSystem,
// masterNodeSystemProcess): adding or selecting a transport is now a single
// polymorphic type rather than three parallel switch arms.
// ============================================================== //

struct System;  // defined below

class NetworkSim {
public:
    virtual ~NetworkSim() = default;

    virtual const char* name() const = 0;

    // Bring the fabric up with the given nodes marked active (index 0 is the
    // master). Resets all transport buffers.
    virtual void initNetwork(const bool nodeActive[fp::kMaxNumNodes]) = 0;

    // Interfaces handed to the daemon, master and nodes.
    virtual fp::FpInterface& daemonItf() = 0;         // daemon end of the USB link
    virtual fp::FpInterface& masterDaemonItf() = 0;   // master end of the USB link
    virtual fp::FpInterface& masterNodeBusItf() = 0;  // master end of the node bus
    virtual fp::FpInterface& nodeBusItf(uint32_t node) = 0;

    virtual uint32_t maxTxCredits() const = 0;
    virtual uint32_t maxRxCredits() const = 0;

    virtual fp::ConnectedNodesFn connectedNodesFn() = 0;
    virtual fp::NumTxInUsbBufferFn numTxInUsbBufferFn() = 0;

    // Advance the fabric one slot: move queued tx packets to their rx buffers
    // and let the master and every active node process them. ignoreNode masks
    // nodes that should appear disconnected this slot.
    virtual void step(System& system, bool ignoreNode[fp::kMaxNumNodes]) = 0;
};

// ============================================================== //
// System under test
//
// Owns the master, the nodes, and (in daemon builds) the daemon, plus the
// board catalogue selected for the run. Constructed fresh per test iteration,
// so iterations no longer share mutable global state and there is no manual
// reset dance between them.
// ============================================================== //

struct System {
    NetworkSim* sim = nullptr;
    fp::Master master;
    std::optional<fp::Node> nodes[fp::kMaxNumNodes];
#ifdef USE_FP_DAEMON
    fp::Daemon daemon;
#endif

    uint32_t numNodes = 0;
    bool ignoreNode[fp::kMaxNumNodes] = {false};
    uint64_t cycles = 0;

    // Board info / field tables for index 0 (master) .. numNodes. Points either
    // into the static catalogue or into this System's own random-board storage
    // below.
    fp::BoardInfo* boardInfo = nullptr;
    fp::FieldTable** tables = nullptr;

    // Random-board storage, owned per-System so iterations share no mutable
    // global state. `arena` backs the field tables/entries/bytes; when the
    // System is destroyed the arena frees them (RAII), so there is no manual
    // free between or after iterations. Unused when running the static catalogue.
    TestArena arena;
    fp::BoardInfo randomBoardInfo[fp::kMaxNumNodes] = {};
    fp::FieldTable* randomTables[fp::kMaxNumNodes] = {nullptr};

#ifdef USE_FP_DAEMON
    // The pump used both as the daemon's DelayFn and by the test scenarios to
    // settle the system. Counts cycles for instrumentation.
    void pump() {
        cycles++;
        sim->step(*this, ignoreNode);
        daemon.processRx();
    }
#endif
};

// Wires `sys` up against `sim` with either the static board catalogue or freshly
// generated random boards. Defined in masterNodeSystem.cpp (it owns the boards).
void buildSystem(System& sys, NetworkSim& sim, bool useRandomBoards, uint32_t numNodes);

// Number of distinct boards in the static (non-random) catalogue defined in
// masterNodeSystem.cpp. The board catalogue itself is private to that file;
// tests reach the active boards through System::boardInfo / System::tables.
inline constexpr uint8_t kMasterNodeSystemNumNodes = 14;

namespace networkSimDetail {

// Shared by every buffered transport: drain the master's and nodes' rx buffers
// after the fabric-specific transfer has run.
inline void driveMasterAndNodes(System& sys, uint8_t numTxPacketsFreed) {
    sys.master.processAllRx(numTxPacketsFreed);
    for (uint32_t i = 1; i < fp::kMaxNumNodes; i++) {
        if (sys.nodes[i].has_value()) {
            sys.nodes[i]->processRxTx();
        }
    }
}

}  // namespace networkSimDetail

// ============================================================== //
// Concrete transports
// ============================================================== //

class MockMicrobusSim : public NetworkSim {
public:
    const char* name() const override { return "MOCK_MICROBUS"; }

    void initNetwork(const bool nodeActive[fp::kMaxNumNodes]) override {
        // The transport functions take a non-const bool[]; they only read it.
        microbusMockInitNetwork(const_cast<bool*>(nodeActive));
        usbMockInitNetwork();
    }

    fp::FpInterface& daemonItf() override { return usbMockDaemonItf; }
    fp::FpInterface& masterDaemonItf() override { return usbMockMasterDaemonItf; }
    fp::FpInterface& masterNodeBusItf() override { return microbusMockMasterNodeBusItf; }
    fp::FpInterface& nodeBusItf(uint32_t node) override { return microbusMockNodeBusItf[node]; }
    uint32_t maxTxCredits() const override { return microbusMockGetMaxTxCredits(); }
    uint32_t maxRxCredits() const override { return usbMockGetMaxRxCredits(); }
    fp::ConnectedNodesFn connectedNodesFn() override { return microbusMockGetConnectedNodesBitField; }
    fp::NumTxInUsbBufferFn numTxInUsbBufferFn() override { return usbMockGetNumTxInUsbBuffer; }

    void step(System& sys, bool ignoreNode[fp::kMaxNumNodes]) override {
        uint8_t numTxPacketsFreed = 0;
        microbusMockTransferAllTxToRx(ignoreNode, &numTxPacketsFreed);
        usbMockTransferAllTxToRx();
        networkSimDetail::driveMasterAndNodes(sys, numTxPacketsFreed);
    }
};

#ifndef USE_MOCK_MICROBUS
class RealMicrobusSim : public NetworkSim {
public:
    const char* name() const override { return "REAL_MICROBUS"; }

    void initNetwork(const bool nodeActive[fp::kMaxNumNodes]) override {
        microbusInitNetwork(const_cast<bool*>(nodeActive));
        usbMockInitNetwork();
    }

    fp::FpInterface& daemonItf() override { return usbMockDaemonItf; }
    fp::FpInterface& masterDaemonItf() override { return usbMockMasterDaemonItf; }
    fp::FpInterface& masterNodeBusItf() override { return microbusMasterNodeItf; }
    fp::FpInterface& nodeBusItf(uint32_t node) override { return microbusNodeItf[node]; }
    uint32_t maxTxCredits() const override { return microbusGetMaxTxCredits(); }
    uint32_t maxRxCredits() const override { return usbMockGetMaxRxCredits(); }
    fp::ConnectedNodesFn connectedNodesFn() override { return microbusGetConnectedNodesBitField; }
    fp::NumTxInUsbBufferFn numTxInUsbBufferFn() override { return usbMockGetNumTxInUsbBuffer; }

    void step(System& sys, bool ignoreNode[fp::kMaxNumNodes]) override {
        uint8_t numTxPacketsFreed = 0;
        microbusTransferAllTxToRx(ignoreNode, &numTxPacketsFreed);
        usbMockTransferAllTxToRx();
        sys.master.processAllRx(numTxPacketsFreed);
        for (uint32_t i = 1; i < fp::kMaxNumNodes; i++) {
            if (sys.nodes[i].has_value()) {
                sys.nodes[i]->processRxTx();
                microbusNodeUpdateTimeUs(microbusNodeItf[i], microbus::kSlotTimeUs);
            }
        }
    }
};
#endif
