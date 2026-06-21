// Copyright (c) 2025 Sean Bremner
// Licensed under the MIT License. See LICENSE file for details.
//
// util.hpp - tiny, dependency-free helpers.
//
// These replace the old MIN/MAX/INCR_AND_WRAP/DECR_AND_WRAP macros and the
// file-scope `seed` global. As plain `constexpr` functions and a small struct
// they are type-safe, debuggable, and free of the double-evaluation hazards
// that macros have.

#pragma once

#include <cstdint>

#include "microbus/src/config.hpp"

namespace microbus {

// Wrap-around add/subtract over a modulus. Used for circular indices and for
// sequence-number arithmetic. `step` is assumed <= modulus.
constexpr uint8_t wrapAdd(uint8_t value, uint8_t step,
                               uint8_t modulus) noexcept {
    return (value >= modulus - step)
               ? static_cast<uint8_t>(step - (modulus - value))
               : static_cast<uint8_t>(value + step);
}

constexpr uint8_t wrapSub(uint8_t value, uint8_t step,
                               uint8_t modulus) noexcept {
    return (value < step) ? static_cast<uint8_t>(modulus - step + value)
                          : static_cast<uint8_t>(value - step);
}

// ---------------------------------------------------------------------------
// Sequence numbers
//
// Sequence numbers live in [0, 254]. The value 255 is reserved as the "null /
// invalid / not-yet-set" sentinel (see SeqNum::kNone in packet.hpp), which is
// why the modulus below is 255 rather than 256.
// ---------------------------------------------------------------------------

inline constexpr uint8_t kSeqModulus = 255;

constexpr uint8_t nextSeq(uint8_t s) noexcept {
    return (s + 1 >= kSeqModulus) ? uint8_t{0} : static_cast<uint8_t>(s + 1);
}

// ---------------------------------------------------------------------------
// Lcg - a minimal linear congruential generator.
//
// Each Node owns one, seeded from its unique id, to randomise join backoff so
// that two simultaneously-powered nodes don't collide forever. Deterministic
// and self-contained (no libc rand(), no shared global state).
// ---------------------------------------------------------------------------

class Lcg {
public:
    constexpr Lcg() noexcept = default;
    explicit constexpr Lcg(uint32_t seed) noexcept { reseed(seed); }

    constexpr void reseed(uint32_t seed) noexcept {
        state_ = seed ? seed : 1u;  // never allow a zero state
    }

    // Returns a value in [0, 0x7FFF].
    constexpr uint16_t next() noexcept {
        state_ = state_ * 1103515245u + 12345u;
        return static_cast<uint16_t>((state_ >> 16) & 0x7FFFu);
    }

    // Returns a value in [0, bound). bound must be > 0.
    constexpr uint16_t nextBelow(uint16_t bound) noexcept {
        return static_cast<uint16_t>(next() % bound);
    }

private:
    uint32_t state_ = 0; // All state should start at zero to avoid filling flash with init data
};

}  // namespace microbus
