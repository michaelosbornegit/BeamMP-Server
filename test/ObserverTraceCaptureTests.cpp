#include "ObserverTraceCapture.h"

#include <array>

#include <doctest/doctest.h>

TEST_CASE("observer trace capture is runtime disabled by default") {
    beammp::observer::ObserverTraceCapture capture;
    std::array<char, 1025> payload {};

    CHECK(capture.IsLockFree());
    CHECK_FALSE(capture.TryCaptureStoredPose(7, 11, payload.data(), 2, 101));
    CHECK_EQ(capture.Disabled(), 1);
    beammp::observer::RawStoredPoseV1 empty {};
    CHECK_FALSE(capture.TryPopForTest(empty));
}

TEST_CASE("observer trace capture enforces record capacity and preserves copied data") {
    beammp::observer::ObserverTraceCapture capture;
    std::array<char, 1025> payload {};
    payload[0] = '{';
    payload[1] = '}';
    capture.SetEnabledForTest(true);

    CHECK(capture.TryCaptureStoredPose(7, 11, payload.data(), 1024, 102));
    CHECK_FALSE(capture.TryCaptureStoredPose(7, 11, payload.data(), 1025, 103));
    CHECK_EQ(capture.Oversize(), 1);

    beammp::observer::RawStoredPoseV1 record {};
    REQUIRE(capture.TryPopForTest(record));
    CHECK_EQ(record.PlayerId, 7);
    CHECK_EQ(record.VehicleId, 11);
    CHECK_EQ(record.PayloadSize, 1024);
    CHECK_EQ(record.RawPose[0], '{');
    CHECK_EQ(record.RawPose[1], '}');
}
