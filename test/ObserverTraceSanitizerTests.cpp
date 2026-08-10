#include "ObserverTraceSanitizer.h"
#include "ObserverTraceWriter.h"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

TEST_CASE("observer sanitizer emits relative allowlisted trace records") {
    beammp::observer::TraceSanitizer sanitizer(1'000'000, 2, 3);
    const auto line = sanitizer.Sanitize(42, 9, 1'020'000, R"({"tim":1.25,"pos":[1,2,3],"rot":[0,0,0,1],"vel":[4,5,6],"rvel":[7,8,9],"ip":"never-copy","key":"never-copy","admin":true,"nested":{"chat":"never-copy"}})");
    REQUIRE(line.has_value());

    const auto output = nlohmann::json::parse(*line);
    CHECK_EQ(output["dt_us"], 20);
    CHECK_EQ(output["player"], 0);
    CHECK_EQ(output["vehicle"], 0);
    CHECK(output.contains("tim"));
    CHECK_FALSE(output.contains("ip"));
    CHECK_FALSE(output.contains("key"));
    CHECK_FALSE(output.contains("admin"));
    CHECK_FALSE(output.contains("nested"));
    CHECK_EQ(output.size(), 8);
}

TEST_CASE("observer sanitizer rejects pre-epoch and over-cap identities") {
    beammp::observer::TraceSanitizer sanitizer(1'000, 1, 1);
    const auto raw = R"({"pos":[1,2,3],"rot":[0,0,0,1],"vel":[4,5,6],"rvel":[7,8,9]})";
    CHECK_FALSE(sanitizer.Sanitize(1, 1, 999, raw).has_value());
    CHECK(sanitizer.Sanitize(1, 1, 1'000, raw).has_value());
    CHECK_FALSE(sanitizer.Sanitize(2, 2, 1'001, raw).has_value());
}

TEST_CASE("observer worker serializes a queued record through the privacy allowlist") {
    beammp::observer::TraceRecordWriter writer(1'000'000, 2, 3);
    beammp::observer::RawStoredPoseV1 record {};
    record.AcceptedMonoNs = 1'025'000;
    record.PlayerId = 42;
    record.VehicleId = 9;
    constexpr std::string_view raw = R"({"pos":[1,2,3],"rot":[0,0,0,1],"vel":[4,5,6],"rvel":[7,8,9],"ip":"never-copy"})";
    record.PayloadSize = static_cast<std::uint16_t>(raw.size());
    std::memcpy(record.RawPose.data(), raw.data(), raw.size());

    const auto line = writer.Serialize(record);
    REQUIRE(line.has_value());
    const auto output = nlohmann::json::parse(*line);
    CHECK_EQ(output["dt_us"], 25);
    CHECK_EQ(output["player"], 0);
    CHECK_EQ(output["vehicle"], 0);
    CHECK_FALSE(output.contains("ip"));
}

TEST_CASE("observer trace writer emits a privacy-minimized epoch header") {
    beammp::observer::TraceRecordWriter writer(1'000'000, 2, 3);

    const auto header = nlohmann::json::parse(writer.Header());
    CHECK_EQ(header["schema"], "beammp.accepted-pose/v1");
    CHECK_EQ(header["clock"], "relative_monotonic_us");
    CHECK_EQ(header["privacy"], "dense-session-ids;kinematics-only");
    CHECK_EQ(header.size(), 3);
    CHECK_FALSE(header.contains("player"));
    CHECK_FALSE(header.contains("vehicle"));
    CHECK_FALSE(header.contains("path"));
}

TEST_CASE("observer trace writer footer contains only aggregate lifecycle evidence") {
    beammp::observer::TraceRecordWriter writer(1'000'000, 2, 3);

    const auto footer = nlohmann::json::parse(writer.Footer({
        .Accepted = 12,
        .Written = 9,
        .ParseRejected = 2,
        .Oversize = 1,
        .EvictedOldest = 3,
        .ContentionDrops = 4,
        .DurationUs = 20'000,
    }));

    CHECK_EQ(footer["footer"], "beammp.accepted-pose/v1");
    CHECK_EQ(footer["accepted"], 12);
    CHECK_EQ(footer["written"], 9);
    CHECK_EQ(footer["parse_rejected"], 2);
    CHECK_EQ(footer["oversize"], 1);
    CHECK_EQ(footer["evicted_oldest"], 3);
    CHECK_EQ(footer["contention_drops"], 4);
    CHECK_EQ(footer["duration_us"], 20'000);
    CHECK_EQ(footer.size(), 8);
    CHECK_FALSE(footer.contains("player"));
    CHECK_FALSE(footer.contains("vehicle"));
    CHECK_FALSE(footer.contains("path"));
    CHECK_FALSE(footer.contains("error"));
}
