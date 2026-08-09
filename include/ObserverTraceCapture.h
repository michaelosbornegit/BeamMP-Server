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
#include <string_view>
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

    struct Metrics final {
        std::uint64_t ValidAttempts {};
        std::uint64_t QueuedSuccesses {};
        std::uint64_t EvictedOldest {};
        std::uint64_t ContentionDrops {};
        std::uint64_t Dequeued {};
        std::size_t Pending {};
    };

    [[nodiscard]] bool TryCaptureStoredPose(std::int32_t playerId, std::int32_t vehicleId, const std::string_view rawPose, std::uint64_t acceptedMonoNs) noexcept {
        if (!mEnabled.load(std::memory_order_acquire)) {
            return false;
        }
        if (playerId < 0 || vehicleId < 0) {
            mInvalidId.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        if (rawPose.empty()) {
            mInvalidPayload.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        if (rawPose.size() > kRawPoseCapacity) {
            mOversize.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        RawStoredPoseV1 record {};
        record.AcceptedMonoNs = acceptedMonoNs;
        record.ProducerSequence = mSequence.fetch_add(1, std::memory_order_relaxed);
        record.PlayerId = playerId;
        record.VehicleId = vehicleId;
        record.PayloadSize = static_cast<std::uint16_t>(rawPose.size());
        std::memcpy(record.RawPose.data(), rawPose.data(), rawPose.size());

        // Reserve the approximate-depth slot before publication so a concurrent
        // consumer cannot decrement it before this producer increments it.
        mPending.fetch_add(1, std::memory_order_relaxed);
        mProducerQueueOperations.fetch_add(1, std::memory_order_relaxed);
        if (mQueue.push(record)) {
            mAccepted.fetch_add(1, std::memory_order_relaxed);
            return true;
        }

        RawStoredPoseV1 discarded {};
        mProducerQueueOperations.fetch_add(1, std::memory_order_relaxed);
        if (mQueue.pop(discarded)) {
            mPending.fetch_sub(1, std::memory_order_relaxed);
            mEvictedOldest.fetch_add(1, std::memory_order_relaxed);
            mProducerQueueOperations.fetch_add(1, std::memory_order_relaxed);
            if (mQueue.push(record)) {
                mAccepted.fetch_add(1, std::memory_order_relaxed);
                return true;
            }
        }
        mPending.fetch_sub(1, std::memory_order_relaxed);
        mContentionDrop.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    void SetEnabledForTest(bool enabled) noexcept { mEnabled.store(enabled, std::memory_order_release); }
    [[nodiscard]] bool TryPopForTest(RawStoredPoseV1& output) noexcept {
        if (!mQueue.pop(output)) return false;
        mPending.fetch_sub(1, std::memory_order_relaxed);
        mDequeued.fetch_add(1, std::memory_order_relaxed);
        return true;
    }
    [[nodiscard]] std::size_t PendingForTest() const noexcept { return mPending.load(std::memory_order_relaxed); }
    [[nodiscard]] Metrics MetricsForTest() const noexcept {
        return {
            mSequence.load(std::memory_order_relaxed),
            mAccepted.load(std::memory_order_relaxed),
            mEvictedOldest.load(std::memory_order_relaxed),
            mContentionDrop.load(std::memory_order_relaxed),
            mDequeued.load(std::memory_order_relaxed),
            mPending.load(std::memory_order_relaxed),
        };
    }
    [[nodiscard]] bool IsLockFree() const noexcept { return mQueue.is_lock_free() && mEnabled.is_lock_free() && mSequence.is_lock_free() && mInvalidId.is_lock_free() && mInvalidPayload.is_lock_free() && mOversize.is_lock_free() && mAccepted.is_lock_free() && mPending.is_lock_free() && mEvictedOldest.is_lock_free() && mContentionDrop.is_lock_free() && mDequeued.is_lock_free() && mProducerQueueOperations.is_lock_free(); }
    [[nodiscard]] std::uint64_t InvalidIds() const noexcept { return mInvalidId.load(std::memory_order_relaxed); }
    [[nodiscard]] std::uint64_t InvalidPayloads() const noexcept { return mInvalidPayload.load(std::memory_order_relaxed); }
    [[nodiscard]] std::uint64_t Oversize() const noexcept { return mOversize.load(std::memory_order_relaxed); }
    [[nodiscard]] std::uint64_t Accepted() const noexcept { return mAccepted.load(std::memory_order_relaxed); }
    [[nodiscard]] std::uint64_t EvictedOldest() const noexcept { return mEvictedOldest.load(std::memory_order_relaxed); }
    [[nodiscard]] std::uint64_t ContentionDrop() const noexcept { return mContentionDrop.load(std::memory_order_relaxed); }
    void ResetProducerQueueOperationCountForTest() noexcept { mProducerQueueOperations.store(0, std::memory_order_relaxed); }
    [[nodiscard]] std::uint64_t ProducerQueueOperationCountForTest() const noexcept { return mProducerQueueOperations.load(std::memory_order_relaxed); }

private:
    boost::lockfree::queue<RawStoredPoseV1, boost::lockfree::capacity<kQueueCapacity>> mQueue {};
    std::atomic<bool> mEnabled { false };
    std::atomic<std::uint64_t> mSequence {};
    std::atomic<std::uint64_t> mInvalidId {};
    std::atomic<std::uint64_t> mInvalidPayload {};
    std::atomic<std::uint64_t> mOversize {};
    std::atomic<std::uint64_t> mAccepted {};
    std::atomic<std::size_t> mPending {};
    std::atomic<std::uint64_t> mEvictedOldest {};
    std::atomic<std::uint64_t> mContentionDrop {};
    std::atomic<std::uint64_t> mDequeued {};
    // Test-only accounting seam: the wrapper may issue at most one push, one
    // eviction pop, and one retry push for a valid producer call.
    std::atomic<std::uint64_t> mProducerQueueOperations {};
};

} // namespace beammp::observer
