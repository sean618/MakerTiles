// Copyright (c) 2025 Sean Bremner
// Licensed under the MIT License. See LICENSE file for details.
//
// diagnostics.hpp - assertions and logging.
//
// Both are compiled out entirely when MICROBUS_LOGGING == 0, so a release
// firmware build pays nothing for them. The logging here is intentionally
// printf-based: it is meant for host-side simulation and bring-up, not for the
// production data path.

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "microbus/src/config.hpp"  // fixed-width integer type aliases

// ---------------------------------------------------------------------------
// Build-time switches
// ---------------------------------------------------------------------------

#define MICROBUS_TARGET_MCU

#ifndef MICROBUS_LOGGING
#ifndef MICROBUS_TARGET_MCU
#define MICROBUS_LOGGING 1
#else
#define MICROBUS_LOGGING 0
#endif
#endif

#ifndef MICROBUS_ASSERT_STRINGS
#ifndef MICROBUS_TARGET_MCU
#define MICROBUS_ASSERT_STRINGS 0
#else
#define MICROBUS_ASSERT_STRINGS 1
#endif
#endif


// Fine-grained category switches (only consulted when MICROBUS_LOGGING is on).
#ifndef MICROBUS_LOG_PACKETS
#define MICROBUS_LOG_PACKETS 0
#endif
#ifndef MICROBUS_LOG_EMPTY_PACKETS
#define MICROBUS_LOG_EMPTY_PACKETS 0
#endif
#ifndef MICROBUS_LOG_NETWORK
#define MICROBUS_LOG_NETWORK 0
#endif
#ifndef MICROBUS_LOG_TX
#define MICROBUS_LOG_TX 0
#endif
#ifndef MICROBUS_LOG_SCHEDULER
#define MICROBUS_LOG_SCHEDULER 0
#endif

namespace microbus {

// Globals owned by the host application / test harness. On real hardware you
// can point `logSink` at a UART-backed FILE* or leave logging disabled.
extern std::FILE* logSink;
extern bool loggingEnabled;
extern uint64_t cycleIndex;

// Out-of-line, weak so the embedding application can override the failure
// behaviour at link time (e.g. to reset the MCU instead of spinning).
void assertFailed(const char* message, std::size_t messageLen) noexcept
    __attribute__((weak));

}  // namespace microbus

// ---------------------------------------------------------------------------
// Logging macros
//
// Macros (rather than functions) so that, when disabled, the format string and
// arguments are never evaluated and produce no code at all.
// ---------------------------------------------------------------------------

#if MICROBUS_LOGGING > 0
#define MB_LOG_IMPL(withCycle, fmt, ...)                                       \
    do {                                                                       \
        if (::microbus::loggingEnabled && ::microbus::logSink) {               \
            if (withCycle) {                                                   \
                std::fprintf(::microbus::logSink, "[Cycle:%llu] " fmt,         \
                             (unsigned long long)::microbus::cycleIndex,       \
                             ##__VA_ARGS__);                                    \
            } else {                                                           \
                std::fprintf(::microbus::logSink, fmt, ##__VA_ARGS__);         \
            }                                                                  \
        }                                                                      \
    } while (0)
#else
#define MB_LOG_IMPL(...) do { } while (0)
#endif

// General log line (prefixed with the current cycle).
#define MB_LOG(...)          MB_LOG_IMPL(true, __VA_ARGS__)
// Continuation on the same line (no cycle prefix, no implicit newline).
#define MB_LOG_CONT(...)     MB_LOG_IMPL(false, __VA_ARGS__)

// Category-scoped helpers. The `if` guard is a compile-time constant, so the
// optimiser drops the whole statement when the category is off.
#define MB_LOG_SCHEDULER(...) do { if (MICROBUS_LOG_SCHEDULER) MB_LOG(__VA_ARGS__); } while (0)
#define MB_LOG_SCHEDULER_CONT(...) do { if (MICROBUS_LOG_SCHEDULER) MB_LOG_CONT(__VA_ARGS__); } while (0)
#define MB_LOG_TX(...)        do { if (MICROBUS_LOG_TX)        MB_LOG(__VA_ARGS__); } while (0)
#define MB_LOG_TX_CONT(...)   do { if (MICROBUS_LOG_TX)        MB_LOG_CONT(__VA_ARGS__); } while (0)
#define MB_LOG_NETWORK(...)   do { if (MICROBUS_LOG_NETWORK)   MB_LOG(__VA_ARGS__); } while (0)
#define MB_LOG_NETWORK_CONT(...) do { if (MICROBUS_LOG_NETWORK) MB_LOG_CONT(__VA_ARGS__); } while (0)

// ---------------------------------------------------------------------------
// Assertions
// ---------------------------------------------------------------------------

#define MB_STRINGIFY(x) #x
#define MB_TOSTRING(x)  MB_STRINGIFY(x)



#if MICROBUS_ASSERT_STRINGS
#define MB_ASSERT(pred, msg)                                                   \
    do {                                                                       \
        if (!(pred)) {                                                         \
            static constexpr char mbAssertMsg_[] =                             \
                __FILE__ ":" MB_TOSTRING(__LINE__) " - " msg;                  \
            ::microbus::assertFailed(mbAssertMsg_, sizeof(mbAssertMsg_));      \
        }                                                                      \
    } while (0)
#else
#define MB_ASSERT(pred, msg)                                                   \
    do {                                                                       \
        if (!(pred)) {                                                         \
            ::microbus::assertFailed(nullptr, 0);                             \
        }                                                                      \
    } while (0)
#endif
