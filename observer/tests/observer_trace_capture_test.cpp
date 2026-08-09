#include "ObserverTraceCapture.h"

#include <array>
#include <cassert>
#include <cstring>
#include <iostream>

int main() {
    using beammp::observer::ObserverTraceCapture;
    using beammp::observer::RawStoredPoseV1;

    ObserverTraceCapture capture;
    assert(capture.IsLockFree());
    std::array<char, 1025> payload {};
    payload[0] = '{';
    payload[1] = '}';
    payload[2] = '\n';
    payload[3] = 'x';

    // Runtime disabled is a single no-op decision and does not enqueue.
    assert(!capture.TryCaptureStoredPose(7, 11, payload.data(), 2, 101));
    assert(capture.Disabled() == 1);

    capture.SetEnabledForTest(true);
    assert(capture.TryCaptureStoredPose(7, 11, payload.data(), 2, 102));
    assert(capture.TryCaptureStoredPose(8, 12, payload.data(), 1024, 103));
    assert(!capture.TryCaptureStoredPose(8, 12, payload.data(), 1025, 104));
    assert(capture.Oversize() == 1);

    RawStoredPoseV1 record {};
    assert(capture.TryPopForTest(record));
    assert(record.PlayerId == 7);
    assert(record.VehicleId == 11);
    assert(record.AcceptedMonoNs == 102);
    assert(record.PayloadSize == 2);
    assert(std::memcmp(record.RawPose.data(), payload.data(), 2) == 0);
    std::cout << "observer trace capture tests passed\n";
}
