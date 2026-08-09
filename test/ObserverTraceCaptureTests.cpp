#include "ObserverTraceCapture.h"

#include <array>
#include <atomic>
#include <cstdlib>
#include <new>
#include <thread>
#include <vector>

#include <doctest/doctest.h>

namespace {
std::atomic<bool> gTrackObserverProducerAllocations { false };
std::atomic<std::size_t> gObserverProducerAllocations {};
}

void* operator new(std::size_t size) {
    if (gTrackObserverProducerAllocations.load(std::memory_order_relaxed)) {
        gObserverProducerAllocations.fetch_add(1, std::memory_order_relaxed);
    }
    if (void* memory = std::malloc(size)) return memory;
    throw std::bad_alloc {};
}

void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }

TEST_CASE("observer trace capture is runtime disabled by default") {
    beammp::observer::ObserverTraceCapture capture;
    std::array<char, 1025> payload {};

    CHECK(capture.IsLockFree());
    CHECK_FALSE(capture.TryCaptureStoredPose(7, 11, std::string_view(payload.data(), 2), 101));
    beammp::observer::RawStoredPoseV1 empty {};
    CHECK_FALSE(capture.TryPopForTest(empty));
}

TEST_CASE("observer trace capture enforces record capacity and preserves copied data") {
    beammp::observer::ObserverTraceCapture capture;
    std::array<char, 1025> payload {};
    payload[0] = '{';
    payload[1] = '}';
    capture.SetEnabledForTest(true);

    CHECK(capture.TryCaptureStoredPose(7, 11, std::string_view(payload.data(), 1024), 102));
    CHECK_FALSE(capture.TryCaptureStoredPose(7, 11, std::string_view(payload.data(), 1025), 103));
    CHECK_EQ(capture.Oversize(), 1);

    beammp::observer::RawStoredPoseV1 record {};
    REQUIRE(capture.TryPopForTest(record));
    CHECK_EQ(record.PlayerId, 7);
    CHECK_EQ(record.VehicleId, 11);
    CHECK_EQ(record.PayloadSize, 1024);
    CHECK_EQ(record.RawPose[0], '{');
    CHECK_EQ(record.RawPose[1], '}');
}

TEST_CASE("observer trace capture retains a newest sentinel after deterministic saturation") {
    beammp::observer::ObserverTraceCapture capture;
    capture.SetEnabledForTest(true);
    const std::string_view payload { "{}" };

    for (std::size_t index = 0; index < beammp::observer::ObserverTraceCapture::kQueueCapacity; ++index) {
        REQUIRE(capture.TryCaptureStoredPose(7, static_cast<std::int32_t>(index), payload, index));
    }

    REQUIRE(capture.TryCaptureStoredPose(8, 9999, payload, 9999));
    CHECK_EQ(capture.EvictedOldest(), 1);
    CHECK_EQ(capture.ContentionDrop(), 0);
    CHECK_EQ(capture.PendingForTest(), beammp::observer::ObserverTraceCapture::kQueueCapacity);

    bool foundSentinel = false;
    beammp::observer::RawStoredPoseV1 record {};
    while (capture.TryPopForTest(record)) {
        foundSentinel = foundSentinel || (record.PlayerId == 8 && record.VehicleId == 9999 && record.AcceptedMonoNs == 9999);
    }
    CHECK(foundSentinel);

    const auto metrics = capture.MetricsForTest();
    CHECK_EQ(metrics.ValidAttempts, beammp::observer::ObserverTraceCapture::kQueueCapacity + 1);
    CHECK_EQ(metrics.QueuedSuccesses, beammp::observer::ObserverTraceCapture::kQueueCapacity + 1);
    CHECK_EQ(metrics.EvictedOldest, 1);
    CHECK_EQ(metrics.Dequeued + metrics.EvictedOldest + metrics.Pending, metrics.QueuedSuccesses);
}

TEST_CASE("observer trace capture producer calls allocate nothing after startup") {
    beammp::observer::ObserverTraceCapture capture;
    const std::string_view payload { "{}" };

    gObserverProducerAllocations.store(0, std::memory_order_relaxed);
    gTrackObserverProducerAllocations.store(true, std::memory_order_relaxed);
    CHECK_FALSE(capture.TryCaptureStoredPose(1, 1, payload, 1));
    capture.SetEnabledForTest(true);
    CHECK(capture.TryCaptureStoredPose(1, 1, payload, 2));
    gTrackObserverProducerAllocations.store(false, std::memory_order_relaxed);

    CHECK_EQ(gObserverProducerAllocations.load(std::memory_order_relaxed), 0);
}

TEST_CASE("observer trace capture accounts for concurrent producers while a consumer drains") {
    beammp::observer::ObserverTraceCapture capture;
    capture.SetEnabledForTest(true);
    constexpr std::size_t producers = 4;
    constexpr std::size_t recordsPerProducer = 256;
    std::atomic<std::size_t> completed {};
    std::atomic<std::size_t> successful {};
    std::vector<std::thread> workers;
    workers.reserve(producers);

    for (std::size_t producer = 0; producer < producers; ++producer) {
        workers.emplace_back([&, producer] {
            for (std::size_t index = 0; index < recordsPerProducer; ++index) {
                if (capture.TryCaptureStoredPose(static_cast<std::int32_t>(producer), static_cast<std::int32_t>(index), "{}", index)) {
                    successful.fetch_add(1, std::memory_order_relaxed);
                }
            }
            completed.fetch_add(1, std::memory_order_release);
        });
    }

    beammp::observer::RawStoredPoseV1 record {};
    while (completed.load(std::memory_order_acquire) != producers || capture.PendingForTest() != 0) {
        const bool popped = capture.TryPopForTest(record);
        static_cast<void>(popped);
    }
    for (auto& worker : workers) worker.join();

    const auto metrics = capture.MetricsForTest();
    CHECK_EQ(metrics.ValidAttempts, producers * recordsPerProducer);
    CHECK_EQ(metrics.QueuedSuccesses + metrics.ContentionDrops, metrics.ValidAttempts);
    CHECK_EQ(metrics.Dequeued + metrics.EvictedOldest + metrics.Pending, metrics.QueuedSuccesses);
    CHECK_EQ(successful.load(std::memory_order_relaxed), metrics.QueuedSuccesses);
}

TEST_CASE("observer trace capture kill switch immediately rejects new records without discarding queued records") {
    beammp::observer::ObserverTraceCapture capture;
    capture.SetEnabledForTest(true);
    REQUIRE(capture.TryCaptureStoredPose(3, 4, "{}", 10));

    capture.SetEnabledForTest(false);
    CHECK_FALSE(capture.TryCaptureStoredPose(3, 5, "{}", 11));

    beammp::observer::RawStoredPoseV1 record {};
    REQUIRE(capture.TryPopForTest(record));
    CHECK_EQ(record.VehicleId, 4);
    const auto metrics = capture.MetricsForTest();
    CHECK_EQ(metrics.ValidAttempts, 1);
    CHECK_EQ(metrics.QueuedSuccesses, 1);
    CHECK_EQ(metrics.Dequeued, 1);
    CHECK_EQ(metrics.Pending, 0);
}
