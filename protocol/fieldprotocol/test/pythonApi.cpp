/*
 * Copyright © 2024 - 2024 Sean Bremner. All rights reserved.>>
 */

#include <cassert>
#include <cstdint>
#include <cstdio>

#include "../src/fpCommon.hpp"
#include "networkSim.hpp"
#include "testCommon.hpp"
#include "usbMock.hpp"

// The Python daemon loads this shared library with ctypes and resolves these
// entry points by name, so they must keep C linkage (no C++ name mangling).
#define EXPORT extern "C"

static std::FILE* logfile = nullptr;

// The system under test persists across the ctypes calls below (init wires it
// up; write_packet / run / read_packet drive it). The Python side plays the
// role of the daemon, so there is no C++ daemon here.
static MockMicrobusSim g_mockSim;
#ifndef USE_MOCK_MICROBUS
static RealMicrobusSim g_realSim;
#endif
static System g_sys;

EXPORT void init(bool mock_microbus) {
    logfile = std::fopen("daemon.log", "w+");
    std::fprintf(logfile, "Start\n");

    unsigned seed = fpInitTestRng();
    std::fprintf(logfile, "Test seed: %u\n", seed);

    NetworkSim* sim = nullptr;
    if (mock_microbus) {
        sim = &g_mockSim;
    } else {
#ifndef USE_MOCK_MICROBUS
        sim = &g_realSim;
#else
        assert(0);  // real microbus not linked into this build
#endif
    }
    buildSystem(g_sys, *sim, false, kMasterNodeSystemNumNodes - 1);
}

EXPORT int start_connection() {
    return 0;  // Return 0 for success, non-zero for failure.
}

EXPORT int write_packet(char* data, int length) {
    return daemonUsbSendTxPacket(reinterpret_cast<uint8_t*>(data),
                                 static_cast<uint16_t>(length));  // Number of bytes written.
}

EXPORT int read_packet(char* buffer, int /*max_length*/) {
    return daemonUsbGetRxPacket(reinterpret_cast<uint8_t*>(buffer));
}

EXPORT void run(int num_frames) {
    for (int i = 0; i < num_frames; i++) {
        g_sys.sim->step(g_sys, g_sys.ignoreNode);
    }
}

EXPORT int num_in_usb_tx_buffer() {
    return usbMockGetNumTxInUsbBuffer();
}

// The Python side resolves a symbol literally named "close". Because <unistd.h>
// (pulled in transitively by the C++ standard library) already declares
// ::close(int), we give the C++ function a distinct name and bind the exported
// symbol with an asm label.
extern "C" void fpClose() asm("close");
void fpClose() {
    if (fp::kFpLogging) {
        std::fprintf(logfile, "End\n");
        std::fclose(logfile);
    }
}
