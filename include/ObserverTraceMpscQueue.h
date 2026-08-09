// BeamMP, the BeamNG.drive multiplayer mod.
// Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Affero General Public License as published
// by the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace beammp::observer {

enum class QueuePushResult : std::uint8_t {
    Inserted,
    Evicted,
    ContentionDrop,
};

// Fixed-storage MPSC queue for a single worker consumer. A producer uses one
// ticket, one slot-state CAS, and one release-store: it never retries or waits.
// A producer that revisits a ready slot replaces that cyclicly oldest slot;
// a slot being copied/written loses only the observer record. The consumer may
// scan a bounded number of slots and is deliberately the only looping side.
template <typename T, std::size_t Capacity>
class FixedMpscLatestQueue final {
    static_assert(Capacity > 0);
    static_assert(std::is_trivially_copyable_v<T>);

public:
    [[nodiscard]] QueuePushResult TryPush(const T& value) noexcept {
        const auto ticket = mNextTicket.fetch_add(1, std::memory_order_relaxed);
        Slot& slot = mSlots[ticket % Capacity];
        auto expected = SlotState::Empty;
        if (slot.State.compare_exchange_strong(expected, SlotState::Writing, std::memory_order_acquire, std::memory_order_relaxed)) {
            slot.Value = value;
            slot.State.store(SlotState::Ready, std::memory_order_release);
            return QueuePushResult::Inserted;
        }
        if (expected != SlotState::Ready) return QueuePushResult::ContentionDrop;
        expected = SlotState::Ready;
        if (!slot.State.compare_exchange_strong(expected, SlotState::Writing, std::memory_order_acquire, std::memory_order_relaxed)) {
            return QueuePushResult::ContentionDrop;
        }
        slot.Value = value;
        slot.State.store(SlotState::Ready, std::memory_order_release);
        return QueuePushResult::Evicted;
    }

    [[nodiscard]] bool TryPop(T& output) noexcept {
        for (std::size_t probe = 0; probe < Capacity; ++probe) {
            Slot& slot = mSlots[mConsumerCursor++ % Capacity];
            auto expected = SlotState::Ready;
            if (!slot.State.compare_exchange_strong(expected, SlotState::Reading, std::memory_order_acquire, std::memory_order_relaxed)) continue;
            output = slot.Value;
            slot.State.store(SlotState::Empty, std::memory_order_release);
            return true;
        }
        return false;
    }

    [[nodiscard]] bool IsLockFree() const noexcept {
        return mNextTicket.is_lock_free() && mSlots.front().State.is_lock_free();
    }

private:
    enum class SlotState : std::uint8_t {
        Empty,
        Writing,
        Ready,
        Reading,
    };

    struct Slot final {
        std::atomic<SlotState> State { SlotState::Empty };
        T Value {};
    };

    std::array<Slot, Capacity> mSlots {};
    std::atomic<std::uint64_t> mNextTicket {};
    // Single worker-owned state; producers do not read or modify it.
    std::uint64_t mConsumerCursor {};
};

} // namespace beammp::observer
