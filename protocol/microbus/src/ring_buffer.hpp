// Copyright (c) 2025 Sean Bremner
// Licensed under the MIT License. See LICENSE file for details.
//
// ring_buffer.hpp - a fixed-capacity FIFO.
//
// This replaces the old family of CIRCULAR_BUFFER_* macros that operated on
// loose head/tail/size variables scattered across call sites. Folding them into
// one small, tested type removes a whole class of "passed the wrong size" and
// "forgot to wrap" bugs, and makes the intent obvious at the use site.
//
// As with the original, one slot is left unused so that "full" and "empty" are
// distinguishable from the head/tail indices alone (capacity == Capacity - 1).
// No heap, no exceptions; sized entirely at compile time.

#pragma once

#include <array>
#include <cstdint>

#include "microbus/src/diagnostics.hpp"
#include "microbus/src/util.hpp"

namespace microbus {

template <typename T, int Capacity>
class RingBuffer {
public:
    static_assert(Capacity > 1, "RingBuffer needs at least 2 slots");

    void clear() noexcept {
        head_ = 0;
        tail_ = 0;
    }

    bool empty() const noexcept { return head_ == tail_; }

    bool full() const noexcept {
        return head_ == wrapAdd(tail_, 1, Capacity);
    }

    // Number of elements currently queued.
    uint8_t size() const noexcept {
        return wrapSub(tail_, head_, Capacity);
    }

    // Append to the back. Asserts (and does nothing) if full - callers are
    // expected to check capacity first, exactly as the old macro contract did.
    void push(const T& value) noexcept {
        if (full()) {
            MB_ASSERT(false, "RingBuffer::push on full buffer");
            return;
        }
        slots_[tail_] = value;
        tail_ = wrapAdd(tail_, 1, Capacity);
    }

    // Pointer to the front element, or nullptr when empty.
    T* front() noexcept { return empty() ? nullptr : &slots_[head_]; }
    const T* front() const noexcept { return empty() ? nullptr : &slots_[head_]; }

    // Drop the front element. Asserts if empty.
    void pop() noexcept {
        if (empty()) {
            MB_ASSERT(false, "RingBuffer::pop on empty buffer");
            return;
        }
        head_ = wrapAdd(head_, 1, Capacity);
    }

    // Visit every queued element in order, front to back, by mutable reference.
    // Useful for in-place "tombstone" deletion where elements are invalidated in
    // place rather than shifted out (they are reclaimed when they reach front).
    template <typename Fn>
    void forEachLive(Fn&& fn) noexcept {
        for (uint8_t i = head_; i != tail_; i = wrapAdd(i, 1, Capacity)) {
            fn(slots_[i]);
        }
    }

private:
    std::array<T, Capacity> slots_{};
    uint8_t head_ = 0;
    uint8_t tail_ = 0;
};

}  // namespace microbus
