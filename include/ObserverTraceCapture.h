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

#include "ObserverTraceMpscQueue.h"

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

// Test-build capture boundary. The producer call is bounded to one fixed-slot
// claim that either inserts, cyclically evicts, or drops. It performs no I/O, JSON parsing,
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
        // Shutdown needs to distinguish an empty queue from a producer that
        // already passed the enabled check but has not published its record.
        mActiveProducers.fetch_add(1, std::memory_order_acq_rel);
        if (!mEnabled.load(std::memory_order_acquire)) {
            mActiveProducers.fetch_sub(1, std::memory_order_release);
            return false;
        }
        if (mProducerAdmissionObservedForTest) {
            mProducerAdmissionObservedForTest->store(true, std::memory_order_release);
            while (!mReleaseProducerAdmissionForTest->load(std::memory_order_acquire)) {}
        }
        if (playerId < 0 || vehicleId < 0) {
            mInvalidId.fetch_add(1, std::memory_order_relaxed);
            mActiveProducers.fetch_sub(1, std::memory_order_release);
            return false;
        }
        if (rawPose.empty()) {
            mInvalidPayload.fetch_add(1, std::memory_order_relaxed);
            mActiveProducers.fetch_sub(1, std::memory_order_release);
            return false;
        }
        if (rawPose.size() > kRawPoseCapacity) {
            mOversize.fetch_add(1, std::memory_order_relaxed);
            mActiveProducers.fetch_sub(1, std::memory_order_release);
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
        const auto result = mQueue.TryPush(record);
        if (result == QueuePushResult::Inserted) {
            mAccepted.fetch_add(1, std::memory_order_relaxed);
            mActiveProducers.fetch_sub(1, std::memory_order_release);
            return true;
        }
        if (result == QueuePushResult::Evicted) {
            // This publication replaces one ready slot, so the optimistic
            // pending increment must not increase the approximate depth.
            mPending.fetch_sub(1, std::memory_order_relaxed);
            mEvictedOldest.fetch_add(1, std::memory_order_relaxed);
            mAccepted.fetch_add(1, std::memory_order_relaxed);
            mActiveProducers.fetch_sub(1, std::memory_order_release);
            return true;
        }
        mPending.fetch_sub(1, std::memory_order_relaxed);
        mContentionDrop.fetch_add(1, std::memory_order_relaxed);
        mActiveProducers.fetch_sub(1, std::memory_order_release);
        return false;
    }

    // The writer or shutdown context can stop new producer publication with a
    // single release-store; it never waits for producer activity.
    void Disable() noexcept { mEnabled.store(false, std::memory_order_release); }
    void SetEnabledForTest(bool enabled) noexcept { mEnabled.store(enabled, std::memory_order_release); }
    [[nodiscard]] bool TryPopForTest(RawStoredPoseV1& output) noexcept {
        if (!mQueue.TryPop(output)) return false;
        mPending.fetch_sub(1, std::memory_order_relaxed);
        mDequeued.fetch_add(1, std::memory_order_relaxed);
        return true;
    }
    [[nodiscard]] std::size_t PendingForTest() const noexcept { return mPending.load(std::memory_order_relaxed); }
    [[nodiscard]] std::size_t ActiveProducersForTest() const noexcept { return mActiveProducers.load(std::memory_order_acquire); }
    void SetProducerAdmissionGateForTest(std::atomic<bool>& observed, std::atomic<bool>& release) noexcept {
        mProducerAdmissionObservedForTest = &observed;
        mReleaseProducerAdmissionForTest = &release;
    }
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
    [[nodiscard]] bool ProducerAtomicsAreLockFreeForTest() const noexcept {
        return mEnabled.is_lock_free() && mActiveProducers.is_lock_free() && mSequence.is_lock_free() && mInvalidId.is_lock_free() && mInvalidPayload.is_lock_free() && mOversize.is_lock_free() && mAccepted.is_lock_free() && mPending.is_lock_free() && mEvictedOldest.is_lock_free() && mContentionDrop.is_lock_free() && mDequeued.is_lock_free() && mProducerQueueOperations.is_lock_free();
    }
    [[nodiscard]] bool IsLockFree() const noexcept { return mQueue.IsLockFree() && ProducerAtomicsAreLockFreeForTest(); }
    [[nodiscard]] std::uint64_t InvalidIds() const noexcept { return mInvalidId.load(std::memory_order_relaxed); }
    [[nodiscard]] std::uint64_t InvalidPayloads() const noexcept { return mInvalidPayload.load(std::memory_order_relaxed); }
    [[nodiscard]] std::uint64_t Oversize() const noexcept { return mOversize.load(std::memory_order_relaxed); }
    [[nodiscard]] std::uint64_t Accepted() const noexcept { return mAccepted.load(std::memory_order_relaxed); }
    [[nodiscard]] std::uint64_t EvictedOldest() const noexcept { return mEvictedOldest.load(std::memory_order_relaxed); }
    [[nodiscard]] std::uint64_t ContentionDrop() const noexcept { return mContentionDrop.load(std::memory_order_relaxed); }
    void ResetProducerQueueOperationCountForTest() noexcept { mProducerQueueOperations.store(0, std::memory_order_relaxed); }
    [[nodiscard]] std::uint64_t ProducerQueueOperationCountForTest() const noexcept { return mProducerQueueOperations.load(std::memory_order_relaxed); }

private:
    FixedMpscLatestQueue<RawStoredPoseV1, kQueueCapacity> mQueue {};
    std::atomic<bool> mEnabled { false };
    std::atomic<std::size_t> mActiveProducers {};
    std::atomic<std::uint64_t> mSequence {};
    std::atomic<std::uint64_t> mInvalidId {};
    std::atomic<std::uint64_t> mInvalidPayload {};
    std::atomic<std::uint64_t> mOversize {};
    std::atomic<std::uint64_t> mAccepted {};
    std::atomic<std::size_t> mPending {};
    std::atomic<std::uint64_t> mEvictedOldest {};
    std::atomic<std::uint64_t> mContentionDrop {};
    std::atomic<std::uint64_t> mDequeued {};
    // Test-only accounting seam: the wrapper issues one bounded slot claim for
    // each valid producer call.
    std::atomic<std::uint64_t> mProducerQueueOperations {};
    // Deterministic test-only interleaving seam. It is configured before any
    // producer starts and is otherwise null, so production capture has no hook.
    std::atomic<bool>* mProducerAdmissionObservedForTest {};
    std::atomic<bool>* mReleaseProducerAdmissionForTest {};
};

} // namespace beammp::observer
