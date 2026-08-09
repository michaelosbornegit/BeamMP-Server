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
#include <cstring>
#include <type_traits>

#include <boost/lockfree/queue.hpp>

namespace beammp::observer {

struct RawStoredPoseV1 final {
    std::uint64_t AcceptedMonoNs {};
    std::uint64_t ProducerSequence {};
    std::int32_t PlayerId {};
    std::int32_t VehicleId {};
    std::uint16_t PayloadSize {};
    std::array<char, 1024> RawPose {};
};
static_assert(std::is_trivially_copyable_v<RawStoredPoseV1>);

// Test-build capture boundary. The producer call is bounded: one push, then at
// most one evict-oldest pop and one retry. It performs no I/O, JSON parsing,
// allocation, waiting, or logging.
class ObserverTraceCapture final {
public:
    static constexpr std::size_t kRawPoseCapacity = 1024;
    static constexpr std::size_t kQueueCapacity = 4096;

    [[nodiscard]] bool TryCaptureStoredPose(std::int32_t playerId, std::int32_t vehicleId, const char* rawPose, std::size_t size, std::uint64_t acceptedMonoNs) noexcept {
        if (!mEnabled.load(std::memory_order_acquire)) {
            mDisabled.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        if (playerId < 0 || vehicleId < 0 || rawPose == nullptr || size == 0 || size > kRawPoseCapacity) {
            mOversize.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        RawStoredPoseV1 record {};
        record.AcceptedMonoNs = acceptedMonoNs;
        record.ProducerSequence = mSequence.fetch_add(1, std::memory_order_relaxed);
        record.PlayerId = playerId;
        record.VehicleId = vehicleId;
        record.PayloadSize = static_cast<std::uint16_t>(size);
        std::memcpy(record.RawPose.data(), rawPose, size);

        if (mQueue.push(record)) {
            mAccepted.fetch_add(1, std::memory_order_relaxed);
            return true;
        }

        RawStoredPoseV1 discarded {};
        if (mQueue.pop(discarded) && mQueue.push(record)) {
            mEvictedOldest.fetch_add(1, std::memory_order_relaxed);
            mAccepted.fetch_add(1, std::memory_order_relaxed);
            return true;
        }
        mContentionDrop.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    void SetEnabledForTest(bool enabled) noexcept { mEnabled.store(enabled, std::memory_order_release); }
    [[nodiscard]] bool TryPopForTest(RawStoredPoseV1& output) noexcept { return mQueue.pop(output); }
    [[nodiscard]] bool IsLockFree() const noexcept { return mQueue.is_lock_free(); }
    [[nodiscard]] std::uint64_t Disabled() const noexcept { return mDisabled.load(std::memory_order_relaxed); }
    [[nodiscard]] std::uint64_t Oversize() const noexcept { return mOversize.load(std::memory_order_relaxed); }
    [[nodiscard]] std::uint64_t Accepted() const noexcept { return mAccepted.load(std::memory_order_relaxed); }
    [[nodiscard]] std::uint64_t EvictedOldest() const noexcept { return mEvictedOldest.load(std::memory_order_relaxed); }
    [[nodiscard]] std::uint64_t ContentionDrop() const noexcept { return mContentionDrop.load(std::memory_order_relaxed); }

private:
    boost::lockfree::queue<RawStoredPoseV1, boost::lockfree::capacity<kQueueCapacity>> mQueue {};
    std::atomic<bool> mEnabled { false };
    std::atomic<std::uint64_t> mSequence {};
    std::atomic<std::uint64_t> mDisabled {};
    std::atomic<std::uint64_t> mOversize {};
    std::atomic<std::uint64_t> mAccepted {};
    std::atomic<std::uint64_t> mEvictedOldest {};
    std::atomic<std::uint64_t> mContentionDrop {};
};

} // namespace beammp::observer
