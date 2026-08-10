#include "ObserverTraceSanitizer.h"
#include "ObserverTraceWriter.h"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sys/stat.h>

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

TEST_CASE("observer trace epoch remains partial until the privacy-safe footer is finalized") {
    const auto directory = std::filesystem::temp_directory_path() / ("beammp-observer-trace-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    const auto partial = directory / "beammp-accepted-pose-test.ndjson.part";
    const auto finalized = directory / "beammp-accepted-pose-test.ndjson";

    {
        beammp::observer::TraceEpochFile epoch(partial, 1'000'000, 2, 3);
        REQUIRE(epoch.IsOpen());
        CHECK(std::filesystem::exists(partial));
        CHECK_FALSE(std::filesystem::exists(finalized));
        REQUIRE(epoch.Append(R"({"pos":[1,2,3],"rot":[0,0,0,1],"vel":[4,5,6],"rvel":[7,8,9]})", 42, 9, 1'020'000));
        CHECK(epoch.Finalize({ .Accepted = 1, .Written = 1, .DurationUs = 20'000 }));
    }

    CHECK_FALSE(std::filesystem::exists(partial));
    REQUIRE(std::filesystem::exists(finalized));
    std::ifstream trace(finalized);
    std::string line;
    REQUIRE(std::getline(trace, line));
    CHECK_EQ(nlohmann::json::parse(line)["schema"], "beammp.accepted-pose/v1");
    REQUIRE(std::getline(trace, line));
    CHECK_EQ(nlohmann::json::parse(line)["player"], 0);
    REQUIRE(std::getline(trace, line));
    CHECK_EQ(nlohmann::json::parse(line)["footer"], "beammp.accepted-pose/v1");
    CHECK_FALSE(std::getline(trace, line));
    std::filesystem::remove_all(directory);
}

TEST_CASE("observer trace epoch creates partial and finalized traces owner-readable only") {
    const auto directory = std::filesystem::temp_directory_path() / ("beammp-observer-modes-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    const auto partial = directory / "beammp-accepted-pose-test.ndjson.part";
    const auto finalized = directory / "beammp-accepted-pose-test.ndjson";

    {
        beammp::observer::TraceEpochFile epoch(partial, 1'000'000, 2, 3);
        REQUIRE(epoch.IsOpen());
        struct stat partialStatus {};
        REQUIRE_EQ(::stat(partial.c_str(), &partialStatus), 0);
        CHECK_EQ(partialStatus.st_mode & 0777, 0600);
        CHECK(epoch.Finalize({}));
    }

    struct stat finalizedStatus {};
    REQUIRE_EQ(::stat(finalized.c_str(), &finalizedStatus), 0);
    CHECK_EQ(finalizedStatus.st_mode & 0777, 0600);
    std::filesystem::remove_all(directory);
}

TEST_CASE("observer trace epoch refuses non-partial filenames without creating a file") {
    const auto directory = std::filesystem::temp_directory_path() / ("beammp-observer-invalid-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    const auto unsafe = directory / "not-an-observer-trace.ndjson";

    {
        beammp::observer::TraceEpochFile epoch(unsafe, 1'000'000, 2, 3);
        CHECK_FALSE(epoch.IsOpen());
    }
    CHECK_FALSE(std::filesystem::exists(unsafe));
    std::filesystem::remove_all(directory);
}

TEST_CASE("observer trace epoch refuses a partial-path symlink without modifying its target") {
    const auto directory = std::filesystem::temp_directory_path() / ("beammp-observer-symlink-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    const auto target = directory / "unrelated.txt";
    const auto partial = directory / "beammp-accepted-pose-test.ndjson.part";
    {
        std::ofstream unrelated(target);
        unrelated << "preserve-this";
    }
    std::filesystem::create_symlink(target.filename(), partial);

    {
        beammp::observer::TraceEpochFile epoch(partial, 1'000'000, 2, 3);
        CHECK_FALSE(epoch.IsOpen());
    }

    std::ifstream unrelated(target);
    std::string contents;
    std::getline(unrelated, contents);
    CHECK_EQ(contents, "preserve-this");
    CHECK(std::filesystem::is_symlink(partial));
    std::filesystem::remove_all(directory);
}
