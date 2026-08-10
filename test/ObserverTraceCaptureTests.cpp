#include "ObserverTraceCapture.h"
#include "ObserverTraceMpscQueue.h"
#include "Client.h"
#include "TServer.h"

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

TEST_CASE("observer fixed MPSC queue overwrites an occupied cyclic slot without producer retries") {
    beammp::observer::FixedMpscLatestQueue<int, 4> queue;

    CHECK_EQ(queue.TryPush(1), beammp::observer::QueuePushResult::Inserted);
    CHECK_EQ(queue.TryPush(2), beammp::observer::QueuePushResult::Inserted);
    CHECK_EQ(queue.TryPush(3), beammp::observer::QueuePushResult::Inserted);
    CHECK_EQ(queue.TryPush(4), beammp::observer::QueuePushResult::Inserted);
    CHECK_EQ(queue.TryPush(5), beammp::observer::QueuePushResult::Evicted);

    bool foundNewest = false;
    int value {};
    while (queue.TryPop(value)) foundNewest = foundNewest || value == 5;
    CHECK(foundNewest);
    CHECK(queue.IsLockFree());
}

TEST_CASE("observer fixed MPSC queue records a deterministic producer collision without overwriting a writing slot") {
    beammp::observer::FixedMpscLatestQueue<int, 4> queue;

    REQUIRE(queue.HoldNextSlotForTest());
    CHECK_EQ(queue.TryPush(42), beammp::observer::QueuePushResult::ContentionDrop);
    queue.ReleaseHeldSlotForTest();
    CHECK_EQ(queue.TryPush(42), beammp::observer::QueuePushResult::Inserted);

    int value {};
    REQUIRE(queue.TryPop(value));
    CHECK_EQ(value, 42);
}

TEST_CASE("observer trace capture is runtime disabled by default") {
    beammp::observer::ObserverTraceCapture capture;
    std::array<char, 1025> payload {};

    CHECK(capture.IsLockFree());
    CHECK_FALSE(capture.TryCaptureStoredPose(7, 11, std::string_view(payload.data(), 2), 101));
    beammp::observer::RawStoredPoseV1 empty {};
    CHECK_FALSE(capture.TryPopForTest(empty));
}

TEST_CASE("observer trace capture proves every producer-visible atomic is lock-free") {
    beammp::observer::ObserverTraceCapture capture;

    CHECK(capture.ProducerAtomicsAreLockFreeForTest());
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

TEST_CASE("observer trace capture accounts for invalid identity and payload drops separately") {
    beammp::observer::ObserverTraceCapture capture;
    capture.SetEnabledForTest(true);

    CHECK_FALSE(capture.TryCaptureStoredPose(-1, 11, "{}", 1));
    CHECK_FALSE(capture.TryCaptureStoredPose(7, -1, "{}", 2));
    CHECK_FALSE(capture.TryCaptureStoredPose(7, 11, "", 3));

    CHECK_EQ(capture.InvalidIds(), 2);
    CHECK_EQ(capture.InvalidPayloads(), 1);
    CHECK_EQ(capture.Oversize(), 0);
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

TEST_CASE("observer trace capture bounds a saturated publish to one atomic slot claim") {
    beammp::observer::ObserverTraceCapture capture;
    capture.SetEnabledForTest(true);

    for (std::size_t index = 0; index < beammp::observer::ObserverTraceCapture::kQueueCapacity; ++index) {
        REQUIRE(capture.TryCaptureStoredPose(1, static_cast<std::int32_t>(index), "{}", index));
    }

    capture.ResetProducerQueueOperationCountForTest();
    REQUIRE(capture.TryCaptureStoredPose(2, 9999, "{}", 9999));
    CHECK_EQ(capture.ProducerQueueOperationCountForTest(), 1);
}

TEST_CASE("observer trace capture kill switch rejects a producer release after off") {
    beammp::observer::ObserverTraceCapture capture;
    capture.SetEnabledForTest(true);
    std::atomic<bool> firstPublished {};
    std::atomic<bool> releaseSecondPublish {};
    std::atomic<bool> secondAccepted { true };

    std::thread producer([&] {
        CHECK(capture.TryCaptureStoredPose(3, 4, "{}", 10));
        firstPublished.store(true, std::memory_order_release);
        while (!releaseSecondPublish.load(std::memory_order_acquire)) {}
        secondAccepted.store(capture.TryCaptureStoredPose(3, 5, "{}", 11), std::memory_order_release);
    });

    while (!firstPublished.load(std::memory_order_acquire)) {}
    capture.SetEnabledForTest(false);
    releaseSecondPublish.store(true, std::memory_order_release);
    producer.join();

    CHECK_FALSE(secondAccepted.load(std::memory_order_acquire));
    CHECK_EQ(capture.Accepted(), 1);
    CHECK_EQ(capture.PendingForTest(), 1);
}

TEST_CASE("observer post-store boundary captures the authenticated client pose only after a successful store") {
    TServer server({});
    TClient client(server, ip::tcp::socket(server.IoCtx()));
    client.SetID(77);
    server.SetObserverTraceEnabledForTest(true);

    constexpr std::string_view pose = R"({"pos":[1,2,3],"rot":[0,0,0,1],"vel":[4,5,6],"rvel":[7,8,9]})";
    server.HandlePositionForTest(client, std::string("Zp:999-12:") + std::string(pose));

    CHECK_EQ(client.GetCarPositionRaw(12), pose);
    beammp::observer::RawStoredPoseV1 record {};
    REQUIRE(server.TryPopObserverTraceForTest(record));
    CHECK_EQ(record.PlayerId, 77);
    CHECK_EQ(record.VehicleId, 12);
    CHECK_EQ(std::string_view(record.RawPose.data(), record.PayloadSize), pose);

    server.HandlePositionForTest(client, "Zp:999-13:not-a-pose");
    CHECK_FALSE(server.TryPopObserverTraceForTest(record));
}

TEST_CASE("observer server runtime starts and stops capture through the test-only control boundary") {
    const auto directory = std::filesystem::temp_directory_path() / ("beammp-observer-server-runtime-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    const beammp::observer::TraceCaptureConfiguration configuration {
        .Enabled = true,
        .Directory = directory,
        .MaximumSeconds = 10,
        .MaximumFileMiB = 16,
        .MaximumTotalMiB = 16,
        .MaximumAgeHours = 1,
    };
    constexpr std::string_view pose = R"({"pos":[1,2,3],"rot":[0,0,0,1],"vel":[4,5,6],"rvel":[7,8,9]})";

    TServer server({});
    REQUIRE(server.StartObserverTraceForTest(configuration, "beammp-accepted-pose-server.ndjson.part", 1'000'000));
    TClient client(server, ip::tcp::socket(server.IoCtx()));
    client.SetID(77);
    server.HandlePositionForTest(client, std::string("Zp:999-12:") + std::string(pose));
    server.StopObserverTraceForTest();

    CHECK(server.ObserverTraceFinalizedForTest());
    CHECK(std::filesystem::exists(directory / "beammp-accepted-pose-server.ndjson"));
    std::filesystem::remove_all(directory);
}

TEST_CASE("observer server starts capture from a complete enabled environment before accepting a stored pose") {
    const auto directory = std::filesystem::temp_directory_path() / ("beammp-observer-server-startup-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    REQUIRE(::setenv("BEAMMP_OBSERVER_TRACE_ENABLED", "true", 1) == 0);
    REQUIRE(::setenv("BEAMMP_OBSERVER_TRACE_DIR", directory.c_str(), 1) == 0);
    REQUIRE(::setenv("BEAMMP_OBSERVER_TRACE_MAX_SECONDS", "10", 1) == 0);
    REQUIRE(::setenv("BEAMMP_OBSERVER_TRACE_MAX_FILE_MIB", "16", 1) == 0);
    REQUIRE(::setenv("BEAMMP_OBSERVER_TRACE_MAX_TOTAL_MIB", "16", 1) == 0);
    REQUIRE(::setenv("BEAMMP_OBSERVER_TRACE_MAX_AGE_HOURS", "1", 1) == 0);

    TServer server({});
    CHECK(server.ObserverTraceRunningForTest());
    TClient client(server, ip::tcp::socket(server.IoCtx()));
    client.SetID(77);
    server.HandlePositionForTest(client, R"(Zp:999-12:{"pos":[1,2,3],"rot":[0,0,0,1],"vel":[4,5,6],"rvel":[7,8,9]}))");
    server.StopObserverTraceForTest();

    CHECK(server.ObserverTraceFinalizedForTest());
    CHECK(::unsetenv("BEAMMP_OBSERVER_TRACE_ENABLED") == 0);
    CHECK(::unsetenv("BEAMMP_OBSERVER_TRACE_DIR") == 0);
    CHECK(::unsetenv("BEAMMP_OBSERVER_TRACE_MAX_SECONDS") == 0);
    CHECK(::unsetenv("BEAMMP_OBSERVER_TRACE_MAX_FILE_MIB") == 0);
    CHECK(::unsetenv("BEAMMP_OBSERVER_TRACE_MAX_TOTAL_MIB") == 0);
    CHECK(::unsetenv("BEAMMP_OBSERVER_TRACE_MAX_AGE_HOURS") == 0);
    std::filesystem::remove_all(directory);
}

TEST_CASE("observer server runtime refuses enablement when producer atomics are not lock-free") {
    const auto directory = std::filesystem::temp_directory_path() / ("beammp-observer-server-lock-free-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    const beammp::observer::TraceCaptureConfiguration configuration {
        .Enabled = true,
        .Directory = directory,
        .MaximumSeconds = 10,
        .MaximumFileMiB = 16,
        .MaximumTotalMiB = 16,
        .MaximumAgeHours = 1,
    };

    TServer server({});
    server.ForceObserverTraceNonLockFreeForTest();

    CHECK_FALSE(server.StartObserverTraceForTest(configuration, "beammp-accepted-pose-lock-free.ndjson.part", 1'000'000));
    CHECK_FALSE(std::filesystem::exists(directory / "beammp-accepted-pose-lock-free.ndjson.part"));
    CHECK_FALSE(std::filesystem::exists(directory / "beammp-accepted-pose-lock-free.ndjson"));
    std::filesystem::remove_all(directory);
}

TEST_CASE("observer server runtime starts a fresh trace epoch after stop") {
    const auto directory = std::filesystem::temp_directory_path() / ("beammp-observer-server-restart-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    const beammp::observer::TraceCaptureConfiguration configuration {
        .Enabled = true,
        .Directory = directory,
        .MaximumSeconds = 10,
        .MaximumFileMiB = 16,
        .MaximumTotalMiB = 16,
        .MaximumAgeHours = 1,
    };

    TServer server({});
    REQUIRE(server.StartObserverTraceForTest(configuration, "beammp-accepted-pose-first.ndjson.part", 1'000'000));
    server.StopObserverTraceForTest();
    CHECK(server.ObserverTraceFinalizedForTest());

    REQUIRE(server.StartObserverTraceForTest(configuration, "beammp-accepted-pose-second.ndjson.part", 2'000'000));
    server.StopObserverTraceForTest();
    CHECK(server.ObserverTraceFinalizedForTest());
    CHECK(std::filesystem::exists(directory / "beammp-accepted-pose-first.ndjson"));
    CHECK(std::filesystem::exists(directory / "beammp-accepted-pose-second.ndjson"));
    std::filesystem::remove_all(directory);
}

TEST_CASE("observer runtime status command exposes only capture state and aggregate lifecycle state") {
    TServer server({});

    CHECK_EQ(server.RunObserverTraceCommandForTest("status"), "observertrace disabled idle accepted=0 pending=0 evicted=0 contention_drop=0");
    CHECK_EQ(server.RunObserverTraceCommandForTest("off"), "observertrace disabled");
    CHECK_EQ(server.RunObserverTraceCommandForTest("on"), "observertrace unavailable");
    CHECK_FALSE(server.ObserverTraceCaptureEnabledForTest());
    CHECK_EQ(server.RunObserverTraceCommandForTest("invalid"), "observertrace invalid command");
}

TEST_CASE("observer runtime status reports aggregate counters without payload data") {
    TServer server({});
    server.SetObserverTraceEnabledForTest(true);
    REQUIRE(server.HandleObserverTracePoseForTest(7, 12, R"({"ip":"private","pos":[1,2,3]})", 10));

    const auto status = server.RunObserverTraceCommandForTest("status");
    CHECK(status.find("accepted=1") != std::string::npos);
    CHECK(status.find("pending=1") != std::string::npos);
    CHECK(status.find("private") == std::string::npos);
    CHECK(status.find("pos") == std::string::npos);
}

TEST_CASE("observer runtime off command disables producers and finalizes asynchronously") {
    const auto directory = std::filesystem::temp_directory_path() / ("beammp-observer-server-off-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    const beammp::observer::TraceCaptureConfiguration configuration {
        .Enabled = true,
        .Directory = directory,
        .MaximumSeconds = 10,
        .MaximumFileMiB = 16,
        .MaximumTotalMiB = 16,
        .MaximumAgeHours = 1,
    };

    TServer server({});
    REQUIRE(server.StartObserverTraceForTest(configuration, "beammp-accepted-pose-off.ndjson.part", 1'000'000));
    TClient client(server, ip::tcp::socket(server.IoCtx()));
    client.SetID(77);
    server.HandlePositionForTest(client, R"(Zp:999-12:{"pos":[1,2,3],"rot":[0,0,0,1],"vel":[4,5,6],"rvel":[7,8,9]}))");
    CHECK_EQ(server.RunObserverTraceCommandForTest("off"), "observertrace disabled");
    CHECK_FALSE(server.ObserverTraceCaptureEnabledForTest());

    for (std::size_t attempt = 0; attempt < 100 && !server.ObserverTraceFinalizedForTest(); ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(server.ObserverTraceFinalizedForTest());
    CHECK(std::filesystem::exists(directory / "beammp-accepted-pose-off.ndjson"));
    server.StopObserverTraceForTest();
    std::filesystem::remove_all(directory);
}
