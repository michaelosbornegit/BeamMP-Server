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

TEST_CASE("observer sanitizer does not consume a vehicle identity when its player identity is rejected") {
    beammp::observer::TraceSanitizer sanitizer(1'000, 1, 2);
    constexpr std::string_view raw = R"({"pos":[1,2,3],"rot":[0,0,0,1],"vel":[4,5,6],"rvel":[7,8,9]})";

    REQUIRE(sanitizer.Sanitize(1, 1, 1'000, raw).has_value());
    CHECK_FALSE(sanitizer.Sanitize(2, 2, 1'001, raw).has_value());

    const auto accepted = sanitizer.Sanitize(1, 2, 1'002, raw);
    REQUIRE(accepted.has_value());
    CHECK_EQ(nlohmann::json::parse(*accepted)["vehicle"], 1);
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

TEST_CASE("observer trace epoch refuses an append that would exceed its finalized byte budget") {
    const auto directory = std::filesystem::temp_directory_path() / ("beammp-observer-byte-budget-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    const auto partial = directory / "beammp-accepted-pose-test.ndjson.part";
    const auto finalized = directory / "beammp-accepted-pose-test.ndjson";
    constexpr std::uintmax_t byteBudget = 600;

    {
        beammp::observer::TraceEpochFile epoch(partial, 1'000'000, 2, 3, byteBudget);
        REQUIRE(epoch.IsOpen());
        REQUIRE(epoch.Append(R"({"pos":[1,2,3],"rot":[0,0,0,1],"vel":[4,5,6],"rvel":[7,8,9]})", 42, 9, 1'020'000));
        CHECK_FALSE(epoch.Append(R"({"pos":[1,2,3],"rot":[0,0,0,1],"vel":[4,5,6],"rvel":[7,8,9]})", 42, 9, 1'040'000));
        CHECK(epoch.Finalize({ .Accepted = 2, .Written = 1, .DurationUs = 40'000 }));
    }

    REQUIRE(std::filesystem::exists(finalized));
    CHECK_LE(std::filesystem::file_size(finalized), byteBudget);
    std::ifstream trace(finalized);
    std::string line;
    REQUIRE(std::getline(trace, line));
    REQUIRE(std::getline(trace, line));
    CHECK_EQ(nlohmann::json::parse(line)["dt_us"], 20);
    REQUIRE(std::getline(trace, line));
    CHECK_EQ(nlohmann::json::parse(line)["footer"], "beammp.accepted-pose/v1");
    CHECK_FALSE(std::getline(trace, line));
    std::filesystem::remove_all(directory);
}

TEST_CASE("observer trace epoch that cannot fit its privacy header leaves no empty partial trace") {
    const auto directory = std::filesystem::temp_directory_path() / ("beammp-observer-header-budget-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    const auto partial = directory / "beammp-accepted-pose-test.ndjson.part";

    {
        beammp::observer::TraceEpochFile epoch(partial, 1'000'000, 2, 3, 1);
        CHECK_FALSE(epoch.IsOpen());
    }

    CHECK_FALSE(std::filesystem::exists(partial));
    std::filesystem::remove_all(directory);
}

TEST_CASE("observer trace epoch refuses to overwrite an existing finalized trace") {
    const auto directory = std::filesystem::temp_directory_path() / ("beammp-observer-no-overwrite-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    const auto partial = directory / "beammp-accepted-pose-test.ndjson.part";
    const auto finalized = directory / "beammp-accepted-pose-test.ndjson";
    {
        std::ofstream existing(finalized);
        existing << "preserve-finalized-trace";
    }

    {
        beammp::observer::TraceEpochFile epoch(partial, 1'000'000, 2, 3);
        CHECK_FALSE(epoch.IsOpen());
    }

    std::ifstream existing(finalized);
    std::string contents;
    std::getline(existing, contents);
    CHECK_EQ(contents, "preserve-finalized-trace");
    CHECK_FALSE(std::filesystem::exists(partial));
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

TEST_CASE("observer trace epoch refuses a non-observer partial filename without creating a file") {
    const auto directory = std::filesystem::temp_directory_path() / ("beammp-observer-invalid-prefix-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    const auto unsafe = directory / "unrelated.ndjson.part";

    {
        beammp::observer::TraceEpochFile epoch(unsafe, 1'000'000, 2, 3);
        CHECK_FALSE(epoch.IsOpen());
    }
    CHECK_FALSE(std::filesystem::exists(unsafe));
    std::filesystem::remove_all(directory);
}

TEST_CASE("observer trace epoch refuses an existing partial trace without destroying recovery evidence") {
    const auto directory = std::filesystem::temp_directory_path() / ("beammp-observer-existing-partial-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    const auto partial = directory / "beammp-accepted-pose-test.ndjson.part";
    {
        std::ofstream existing(partial);
        existing << "preserve-partial-recovery-evidence";
    }

    {
        beammp::observer::TraceEpochFile epoch(partial, 1'000'000, 2, 3);
        CHECK_FALSE(epoch.IsOpen());
    }

    std::ifstream existing(partial);
    std::string contents;
    std::getline(existing, contents);
    CHECK_EQ(contents, "preserve-partial-recovery-evidence");
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

TEST_CASE("observer trace epoch refuses a partial path beneath a symlinked directory") {
    const auto directory = std::filesystem::temp_directory_path() / ("beammp-observer-parent-symlink-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto actualDirectory = directory / "dedicated";
    const auto linkedDirectory = directory / "linked";
    std::filesystem::create_directories(actualDirectory);
    std::filesystem::create_directory_symlink(actualDirectory.filename(), linkedDirectory);
    const auto partial = linkedDirectory / "beammp-accepted-pose-test.ndjson.part";
    const auto target = actualDirectory / "beammp-accepted-pose-test.ndjson.part";

    {
        beammp::observer::TraceEpochFile epoch(partial, 1'000'000, 2, 3);
        CHECK_FALSE(epoch.IsOpen());
    }

    CHECK_FALSE(std::filesystem::exists(target));
    std::filesystem::remove_all(directory);
}

TEST_CASE("observer retention deletes only aged finalized trace files and refuses symlinks") {
    const auto directory = std::filesystem::temp_directory_path() / ("beammp-observer-retention-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    const auto expiredTrace = directory / "beammp-accepted-pose-old.ndjson";
    const auto freshTrace = directory / "beammp-accepted-pose-fresh.ndjson";
    const auto unrelated = directory / "unrelated-old.ndjson";
    const auto protectedTarget = directory / "protected.txt";
    const auto traceSymlink = directory / "beammp-accepted-pose-link.ndjson";
    for (const auto& path : { expiredTrace, freshTrace, unrelated, protectedTarget }) std::ofstream(path) << "preserve";
    std::filesystem::create_symlink(protectedTarget.filename(), traceSymlink);

    const auto now = std::filesystem::file_time_type::clock::now();
    std::filesystem::last_write_time(expiredTrace, now - std::chrono::hours(2));
    std::filesystem::last_write_time(unrelated, now - std::chrono::hours(2));
    std::filesystem::last_write_time(traceSymlink, now - std::chrono::hours(2));

    CHECK_EQ(beammp::observer::TraceRetention::DeleteFinalizedOlderThan(directory, now - std::chrono::hours(1)), 1);
    CHECK_FALSE(std::filesystem::exists(expiredTrace));
    CHECK(std::filesystem::exists(freshTrace));
    CHECK(std::filesystem::exists(unrelated));
    CHECK(std::filesystem::is_symlink(traceSymlink));
    std::ifstream target(protectedTarget);
    std::string contents;
    std::getline(target, contents);
    CHECK_EQ(contents, "preserve");
    std::filesystem::remove_all(directory);
}

TEST_CASE("observer retention refuses an empty observer trace basename") {
    const auto directory = std::filesystem::temp_directory_path() / ("beammp-observer-empty-basename-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    const auto malformedTrace = directory / "beammp-accepted-pose-.ndjson";
    std::ofstream(malformedTrace) << "preserve";
    const auto now = std::filesystem::file_time_type::clock::now();
    std::filesystem::last_write_time(malformedTrace, now - std::chrono::hours(2));

    CHECK_EQ(beammp::observer::TraceRetention::DeleteFinalizedOlderThan(directory, now - std::chrono::hours(1)), 0);
    CHECK(std::filesystem::exists(malformedTrace));
    std::ifstream trace(malformedTrace);
    std::string contents;
    std::getline(trace, contents);
    CHECK_EQ(contents, "preserve");
    std::filesystem::remove_all(directory);
}

TEST_CASE("observer retention trims only the oldest finalized traces to the total-size budget") {
    const auto directory = std::filesystem::temp_directory_path() / ("beammp-observer-total-retention-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    const auto oldestTrace = directory / "beammp-accepted-pose-oldest.ndjson";
    const auto middleTrace = directory / "beammp-accepted-pose-middle.ndjson";
    const auto newestTrace = directory / "beammp-accepted-pose-newest.ndjson";
    const auto unrelated = directory / "unrelated.ndjson";
    const auto protectedTarget = directory / "protected.txt";
    const auto traceSymlink = directory / "beammp-accepted-pose-link.ndjson";
    std::ofstream(oldestTrace) << "1111";
    std::ofstream(middleTrace) << "2222";
    std::ofstream(newestTrace) << "3333";
    std::ofstream(unrelated) << "4444";
    std::ofstream(protectedTarget) << "preserve";
    std::filesystem::create_symlink(protectedTarget.filename(), traceSymlink);

    const auto now = std::filesystem::file_time_type::clock::now();
    std::filesystem::last_write_time(oldestTrace, now - std::chrono::hours(3));
    std::filesystem::last_write_time(middleTrace, now - std::chrono::hours(2));
    std::filesystem::last_write_time(newestTrace, now - std::chrono::hours(1));

    CHECK_EQ(beammp::observer::TraceRetention::TrimFinalizedToTotalBytes(directory, 8), 1);
    CHECK_FALSE(std::filesystem::exists(oldestTrace));
    CHECK(std::filesystem::exists(middleTrace));
    CHECK(std::filesystem::exists(newestTrace));
    CHECK(std::filesystem::exists(unrelated));
    CHECK(std::filesystem::is_symlink(traceSymlink));
    std::ifstream target(protectedTarget);
    std::string contents;
    std::getline(target, contents);
    CHECK_EQ(contents, "preserve");
    std::filesystem::remove_all(directory);
}

TEST_CASE("observer trace finalization preserves a finalized file created after epoch open") {
    const auto directory = std::filesystem::temp_directory_path() / ("beammp-observer-finalize-race-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    const auto partial = directory / "beammp-accepted-pose-test.ndjson.part";
    const auto finalized = directory / "beammp-accepted-pose-test.ndjson";

    {
        beammp::observer::TraceEpochFile epoch(partial, 1'000'000, 2, 3);
        REQUIRE(epoch.IsOpen());
        std::ofstream(finalized) << "preserve-finalized-trace";

        CHECK_FALSE(epoch.Finalize({}));
        CHECK(std::filesystem::exists(partial));
    }

    std::ifstream existing(finalized);
    std::string contents;
    std::getline(existing, contents);
    CHECK_EQ(contents, "preserve-finalized-trace");
    std::filesystem::remove_all(directory);
}

TEST_CASE("observer worker rotation reaches duration and size boundaries without unsigned timestamp wrap") {
    const beammp::observer::TraceEpochRotationPolicy policy {
        .MaximumDurationNs = 1'000,
        .MaximumFileBytes = 600,
    };

    CHECK_FALSE(policy.ShouldRotate(10'000, 10'999, 599));
    CHECK(policy.ShouldRotate(10'000, 11'000, 599));
    CHECK(policy.ShouldRotate(10'000, 10'001, 600));
    CHECK_FALSE(policy.ShouldRotate(10'000, 9'999, 599));
}

TEST_CASE("observer trace epoch exposes rotation at its own duration and byte boundaries") {
    const auto directory = std::filesystem::temp_directory_path() / ("beammp-observer-epoch-rotation-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    const auto partial = directory / "beammp-accepted-pose-test.ndjson.part";
    {
        beammp::observer::TraceEpochFile epoch(partial, 10'000, 2, 3, 600);
        REQUIRE(epoch.IsOpen());
        const beammp::observer::TraceEpochRotationPolicy durationPolicy {
            .MaximumDurationNs = 1'000,
            .MaximumFileBytes = 0,
        };
        const beammp::observer::TraceEpochRotationPolicy bytePolicy {
            .MaximumDurationNs = 0,
            .MaximumFileBytes = epoch.BytesWritten(),
        };
        CHECK_FALSE(epoch.ShouldRotate(durationPolicy, 10'999));
        CHECK(epoch.ShouldRotate(durationPolicy, 11'000));
        CHECK_FALSE(epoch.ShouldRotate(durationPolicy, 9'999));
        CHECK(epoch.ShouldRotate(bytePolicy, 10'001));
    }

    std::filesystem::remove_all(directory);
}

TEST_CASE("observer epoch lifecycle finalizes a rotated trace before resetting dense identities") {
    const auto directory = std::filesystem::temp_directory_path() / ("beammp-observer-lifecycle-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    const auto firstPartial = directory / "beammp-accepted-pose-first.ndjson.part";
    const auto secondPartial = directory / "beammp-accepted-pose-second.ndjson.part";
    const auto firstFinal = directory / "beammp-accepted-pose-first.ndjson";
    const auto secondFinal = directory / "beammp-accepted-pose-second.ndjson";
    constexpr std::string_view raw = R"({"pos":[1,2,3],"rot":[0,0,0,1],"vel":[4,5,6],"rvel":[7,8,9]})";

    beammp::observer::TraceEpochLifecycle lifecycle(
        { .MaximumDurationNs = 1'000, .MaximumFileBytes = 0 }, 2, 3);
    REQUIRE(lifecycle.Start(firstPartial, 10'000));
    REQUIRE(lifecycle.Append(raw, 42, 9, 10'500));
    REQUIRE(lifecycle.RotateIfNeeded(11'000, { .Accepted = 1, .Written = 1, .DurationUs = 1 }, secondPartial));

    CHECK(std::filesystem::exists(firstFinal));
    CHECK(std::filesystem::exists(secondPartial));
    REQUIRE(lifecycle.Append(raw, 73, 21, 11'500));
    REQUIRE(lifecycle.Finalize({ .Accepted = 1, .Written = 1, .DurationUs = 1 }));
    REQUIRE(std::filesystem::exists(secondFinal));

    std::ifstream trace(secondFinal);
    std::string line;
    REQUIRE(std::getline(trace, line)); // header
    REQUIRE(std::getline(trace, line)); // first record of a fresh epoch
    const auto record = nlohmann::json::parse(line);
    CHECK_EQ(record["player"], 0);
    CHECK_EQ(record["vehicle"], 0);
    std::filesystem::remove_all(directory);
}

TEST_CASE("observer epoch lifecycle keeps its active epoch when a rotation successor cannot open") {
    const auto directory = std::filesystem::temp_directory_path() / ("beammp-observer-rotation-successor-failure-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    const auto firstPartial = directory / "beammp-accepted-pose-first.ndjson.part";
    const auto firstFinal = directory / "beammp-accepted-pose-first.ndjson";
    const auto blockedNextPartial = directory / "beammp-accepted-pose-blocked.ndjson.part";
    const auto blockedNextFinal = directory / "beammp-accepted-pose-blocked.ndjson";
    std::ofstream(blockedNextFinal) << "preserve-existing-final";

    beammp::observer::TraceEpochLifecycle lifecycle(
        { .MaximumDurationNs = 1'000, .MaximumFileBytes = 0 }, 2, 3);
    REQUIRE(lifecycle.Start(firstPartial, 10'000));

    CHECK_FALSE(lifecycle.RotateIfNeeded(11'000, {}, blockedNextPartial));
    CHECK(lifecycle.IsOpen());
    CHECK(std::filesystem::exists(firstPartial));
    CHECK_FALSE(std::filesystem::exists(firstFinal));
    CHECK(std::filesystem::exists(blockedNextFinal));

    REQUIRE(lifecycle.Finalize({}));
    std::filesystem::remove_all(directory);
}
