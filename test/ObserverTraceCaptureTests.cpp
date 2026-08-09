#include "ObserverTraceCapture.h"

#include <array>

#include <doctest/doctest.h>

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
}
