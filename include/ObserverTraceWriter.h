// BeamMP, the BeamNG.drive multiplayer mod.
// Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Affero General Public License as published
// by the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

// Test-build-only worker-side record serializer. Never call from packet producers.
#pragma once

#include <chrono>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "../observer/AcceptedPoseFeed.h"
#include "ObserverTraceCapture.h"
#include "ObserverTraceSanitizer.h"

namespace beammp::observer {

// Offline replay receives completed sanitizer output only; it does not accept
// or serialize raw producer payloads.
[[nodiscard]] inline std::optional<AcceptedPose> ReplaySanitizedTraceRecord(const std::string_view line) {
    const auto input = nlohmann::json::parse(line, nullptr, false);
    if (input.is_discarded() || !input.is_object()) return std::nullopt;

    const auto unsignedValue = [&input](const char* key) -> std::optional<std::uint64_t> {
        if (!input.contains(key)) return std::nullopt;
        const auto& value = input[key];
        if (value.is_number_unsigned()) return value.get<std::uint64_t>();
        if (!value.is_number_integer()) return std::nullopt;
        const auto signedValue = value.get<std::int64_t>();
        if (signedValue < 0) return std::nullopt;
        return static_cast<std::uint64_t>(signedValue);
    };
    const auto timestamp = unsignedValue("dt_us");
    const auto player = unsignedValue("player");
    const auto vehicle = unsignedValue("vehicle");
    if (!timestamp || !player || !vehicle || *player > std::numeric_limits<std::uint16_t>::max() || *vehicle > std::numeric_limits<std::uint16_t>::max()) return std::nullopt;

    const auto floats = [&input](const char* key, const std::size_t expectedSize) -> std::optional<std::vector<float>> {
        if (!input.contains(key) || !input[key].is_array() || input[key].size() != expectedSize) return std::nullopt;
        std::vector<float> result;
        result.reserve(expectedSize);
        for (const auto& value : input[key]) {
            if (!value.is_number()) return std::nullopt;
            const auto number = value.get<double>();
            const auto converted = static_cast<float>(number);
            if (!std::isfinite(number) || !std::isfinite(converted)) return std::nullopt;
            result.push_back(converted);
        }
        return result;
    };
    const auto pos = floats("pos", 3);
    const auto rot = floats("rot", 4);
    const auto vel = floats("vel", 3);
    const auto rvel = floats("rvel", 3);
    if (!pos || !rot || !vel || !rvel) return std::nullopt;

    return AcceptedPose {
        .timestamp_us = *timestamp,
        .player_id = static_cast<std::uint16_t>(*player),
        .vehicle_id = static_cast<std::uint16_t>(*vehicle),
        .position = { (*pos)[0], (*pos)[1], (*pos)[2] },
        .rotation = { (*rot)[0], (*rot)[1], (*rot)[2], (*rot)[3] },
        .velocity = { (*vel)[0], (*vel)[1], (*vel)[2] },
        .angular_velocity = { (*rvel)[0], (*rvel)[1], (*rvel)[2] },
    };
}

class TraceRecordWriter final {
public:
    struct FooterMetrics final {
        std::uint64_t Accepted {};
        std::uint64_t Written {};
        std::uint64_t ParseRejected {};
        std::uint64_t Oversize {};
        std::uint64_t EvictedOldest {};
        std::uint64_t ContentionDrops {};
        std::uint64_t DurationUs {};
    };

    TraceRecordWriter(const std::uint64_t traceStartMonoNs, const std::size_t maxPlayers, const std::size_t maxVehicles)
        : mSanitizer(traceStartMonoNs, maxPlayers, maxVehicles) { }

    // This is emitted once per writer-owned trace epoch. It deliberately carries
    // no raw identity, source path, configuration, or host metadata.
    [[nodiscard]] std::string Header() const {
        return R"({"schema":"beammp.accepted-pose/v1","clock":"relative_monotonic_us","privacy":"dense-session-ids;kinematics-only"})";
    }

    [[nodiscard]] std::string Footer(const FooterMetrics& metrics) const {
        return nlohmann::json {
            {"footer", "beammp.accepted-pose/v1"}, {"accepted", metrics.Accepted}, {"written", metrics.Written},
            {"parse_rejected", metrics.ParseRejected}, {"oversize", metrics.Oversize},
            {"evicted_oldest", metrics.EvictedOldest}, {"contention_drops", metrics.ContentionDrops},
            {"duration_us", metrics.DurationUs},
        }.dump();
    }

    [[nodiscard]] std::optional<std::string> Serialize(const RawStoredPoseV1& record) {
        if (record.PayloadSize > record.RawPose.size()) return std::nullopt;
        return mSanitizer.Sanitize(record.PlayerId, record.VehicleId, record.AcceptedMonoNs,
            std::string_view(record.RawPose.data(), record.PayloadSize));
    }

private:
    TraceSanitizer mSanitizer;
};

// Retention is worker-side only. It touches a direct, existing non-symlink
// directory and deletes only aged finalized observer trace regular files.
class TraceRetention final {
public:
    [[nodiscard]] static std::size_t DeleteFinalizedOlderThan(const std::filesystem::path& directory, const std::filesystem::file_time_type cutoff) {
        std::error_code error;
        const auto absoluteDirectory = std::filesystem::absolute(directory, error).lexically_normal();
        if (error) return 0;
        const auto resolvedDirectory = std::filesystem::weakly_canonical(directory, error);
        if (error || resolvedDirectory != absoluteDirectory) return 0;
        const auto directoryStatus = std::filesystem::symlink_status(directory, error);
        if (error || !std::filesystem::is_directory(directoryStatus) || std::filesystem::is_symlink(directoryStatus)) return 0;

        std::size_t deleted {};
        std::filesystem::directory_iterator iterator(directory, error);
        const std::filesystem::directory_iterator end;
        while (!error && iterator != end) {
            const auto path = iterator->path();
            const auto filename = path.filename().string();
            const auto status = std::filesystem::symlink_status(path, error);
            if (!error && std::filesystem::is_regular_file(status) && !std::filesystem::is_symlink(status)
                && HasObserverFinalName(filename)) {
                const auto modified = std::filesystem::last_write_time(path, error);
                if (!error && modified < cutoff && std::filesystem::remove(path, error)) ++deleted;
            }
            error.clear();
            iterator.increment(error);
        }
        return deleted;
    }

    // Enforce the total finalized-trace quota by removing oldest matching regular
    // files first. Unrelated entries and all symlinks remain outside this scope.
    [[nodiscard]] static std::size_t TrimFinalizedToTotalBytes(const std::filesystem::path& directory, const std::uintmax_t maximumBytes) {
        std::error_code error;
        const auto absoluteDirectory = std::filesystem::absolute(directory, error).lexically_normal();
        if (error) return 0;
        const auto resolvedDirectory = std::filesystem::weakly_canonical(directory, error);
        if (error || resolvedDirectory != absoluteDirectory) return 0;
        const auto directoryStatus = std::filesystem::symlink_status(directory, error);
        if (error || !std::filesystem::is_directory(directoryStatus) || std::filesystem::is_symlink(directoryStatus)) return 0;

        struct Candidate final {
            std::filesystem::path Path;
            std::filesystem::file_time_type Modified;
            std::uintmax_t Size;
        };
        std::vector<Candidate> candidates;
        std::uintmax_t totalBytes {};
        std::filesystem::directory_iterator iterator(directory, error);
        const std::filesystem::directory_iterator end;
        while (!error && iterator != end) {
            const auto path = iterator->path();
            const auto filename = path.filename().string();
            const auto status = std::filesystem::symlink_status(path, error);
            if (!error && std::filesystem::is_regular_file(status) && !std::filesystem::is_symlink(status)
                && HasObserverFinalName(filename)) {
                const auto size = std::filesystem::file_size(path, error);
                const auto modified = std::filesystem::last_write_time(path, error);
                if (!error) {
                    candidates.push_back({ path, modified, size });
                    totalBytes += size;
                }
            }
            error.clear();
            iterator.increment(error);
        }
        if (error || totalBytes <= maximumBytes) return 0;

        std::sort(candidates.begin(), candidates.end(), [](const Candidate& left, const Candidate& right) {
            return left.Modified == right.Modified ? left.Path < right.Path : left.Modified < right.Modified;
        });
        std::size_t deleted {};
        for (const auto& candidate : candidates) {
            if (totalBytes <= maximumBytes) break;
            if (std::filesystem::remove(candidate.Path, error)) {
                totalBytes -= candidate.Size;
                ++deleted;
            }
            error.clear();
        }
        return deleted;
    }

private:
    [[nodiscard]] static bool HasObserverFinalName(const std::string_view filename) {
        constexpr std::string_view prefix { "beammp-accepted-pose-" };
        constexpr std::string_view suffix { ".ndjson" };
        return filename.starts_with(prefix) && filename.ends_with(suffix) && filename.size() > prefix.size() + suffix.size();
    }
};

// Worker-owned rotation decision. Producer timestamps can arrive out of order,
// so an earlier timestamp must never underflow into a false rotation.
struct TraceEpochRotationPolicy final {
    std::uint64_t MaximumDurationNs {};
    std::uintmax_t MaximumFileBytes {};

    [[nodiscard]] bool ShouldRotate(const std::uint64_t traceStartMonoNs, const std::uint64_t currentMonoNs,
        const std::uintmax_t currentFileBytes) const noexcept {
        const bool durationReached = MaximumDurationNs != 0 && currentMonoNs >= traceStartMonoNs
            && currentMonoNs - traceStartMonoNs >= MaximumDurationNs;
        const bool sizeReached = MaximumFileBytes != 0 && currentFileBytes >= MaximumFileBytes;
        return durationReached || sizeReached;
    }
};

// Worker-side epoch file only. A trace remains a .part file unless a complete
// allowlisted footer has been flushed and the atomic rename succeeds.
class TraceEpochFile final {
public:
    TraceEpochFile(const std::filesystem::path& partialPath, const std::uint64_t traceStartMonoNs, const std::size_t maxPlayers, const std::size_t maxVehicles,
        const std::uintmax_t maximumBytes = std::numeric_limits<std::uintmax_t>::max())
        : mPartialPath(partialPath), mFinalPath(FinalPath(partialPath)), mTraceStartMonoNs(traceStartMonoNs), mWriter(traceStartMonoNs, maxPlayers, maxVehicles), mMaximumBytes(maximumBytes) {
        if (!HasObserverPartialName(mPartialPath) || !HasSafeParentDirectory(mPartialPath)) return;
        std::error_code statusError;
        const auto finalStatus = std::filesystem::symlink_status(mFinalPath, statusError);
        // A final trace is immutable evidence: never open a matching partial
        // when finalization could replace an existing file.
        if (std::filesystem::exists(finalStatus) || std::filesystem::is_symlink(finalStatus)
            || (statusError && statusError != std::errc::no_such_file_or_directory)) return;
        statusError.clear();
        const auto partialStatus = std::filesystem::symlink_status(mPartialPath, statusError);
        // A leftover partial is recovery evidence, not a reusable output path.
        // Refuse it rather than truncating a prior incomplete trace at startup.
        if (std::filesystem::exists(partialStatus) || std::filesystem::is_symlink(partialStatus)
            || (statusError && statusError != std::errc::no_such_file_or_directory)) return;
        mStream.open(mPartialPath, std::ios::out | std::ios::trunc);
        if (!mStream.is_open()) return;
        std::error_code permissionError;
        std::filesystem::permissions(mPartialPath,
            std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
            std::filesystem::perm_options::replace, permissionError);
        if (permissionError) {
            mStream.close();
            return;
        }
        const auto header = mWriter.Header();
        if (!BytesFit(header.size() + 1, 0)) {
            mStream.close();
            // No header or sample was committed, so this newly-created empty
            // partial is not recovery evidence and must not consume its name.
            std::filesystem::remove(mPartialPath, permissionError);
            return;
        }
        mStream << header << '\n';
        mOpen = static_cast<bool>(mStream);
        if (mOpen) {
            mBytesWritten = header.size() + 1;
            const auto maximum = std::numeric_limits<std::uint64_t>::max();
            mFooterReserveBytes = mWriter.Footer({ maximum, maximum, maximum, maximum, maximum, maximum, maximum }).size() + 1;
        }
        if (!mOpen) mStream.close();
    }

    ~TraceEpochFile() { if (mStream.is_open()) mStream.close(); }

    TraceEpochFile(const TraceEpochFile&) = delete;
    TraceEpochFile& operator=(const TraceEpochFile&) = delete;

    [[nodiscard]] bool IsOpen() const noexcept { return mOpen; }

    // Check the immutable destination immediately before a lifecycle opens a
    // successor. This avoids creating an unused .part when finalization is
    // already known to be impossible because another actor won the final name.
    [[nodiscard]] bool CanFinalize() const {
        if (!mOpen || mFinalized) return false;
        std::error_code error;
        const auto finalStatus = std::filesystem::symlink_status(mFinalPath, error);
        return (error == std::errc::no_such_file_or_directory)
            || (!error && !std::filesystem::exists(finalStatus) && !std::filesystem::is_symlink(finalStatus));
    }

    [[nodiscard]] std::uintmax_t BytesWritten() const noexcept { return mBytesWritten; }

    [[nodiscard]] bool ShouldRotate(const TraceEpochRotationPolicy& policy, const std::uint64_t currentMonoNs) const noexcept {
        return policy.ShouldRotate(mTraceStartMonoNs, currentMonoNs, mBytesWritten);
    }

    [[nodiscard]] bool Append(const std::string_view rawPose, const std::int32_t playerId, const std::int32_t vehicleId, const std::uint64_t acceptedMonoNs) {
        if (!mOpen) return false;
        RawStoredPoseV1 record {};
        if (rawPose.size() > record.RawPose.size()) return false;
        record.PlayerId = playerId;
        record.VehicleId = vehicleId;
        record.AcceptedMonoNs = acceptedMonoNs;
        record.PayloadSize = static_cast<std::uint16_t>(rawPose.size());
        std::memcpy(record.RawPose.data(), rawPose.data(), rawPose.size());
        const auto line = mWriter.Serialize(record);
        if (!line || !BytesFit(line->size() + 1, mFooterReserveBytes)) return false;
        mStream << *line << '\n';
        if (!mStream) return false;
        mBytesWritten += line->size() + 1;
        return true;
    }

    [[nodiscard]] bool Finalize(const TraceRecordWriter::FooterMetrics& metrics) {
        if (!mOpen || mFinalized) return false;
        const auto footer = mWriter.Footer(metrics);
        if (!BytesFit(footer.size() + 1, 0)) return false;
        mStream << footer << '\n';
        mStream.flush();
        if (!mStream) return false;
        mBytesWritten += footer.size() + 1;
        mStream.close();
        std::error_code error;
        // rename() can replace an existing destination on POSIX. Atomically
        // create the final name as a hard link instead: it fails if another
        // actor created the immutable finalized trace after this epoch opened.
        std::filesystem::create_hard_link(mPartialPath, mFinalPath, error);
        if (error) {
            mOpen = false;
            return false;
        }
        std::filesystem::remove(mPartialPath, error);
        if (error) {
            mOpen = false;
            return false;
        }
        mFinalized = true;
        mOpen = false;
        return true;
    }

private:
    [[nodiscard]] bool BytesFit(const std::uintmax_t additionalBytes, const std::uintmax_t reservedBytes) const noexcept {
        if (mBytesWritten > mMaximumBytes) return false;
        const auto remaining = mMaximumBytes - mBytesWritten;
        return reservedBytes <= remaining && additionalBytes <= remaining - reservedBytes;
    }

    [[nodiscard]] static bool HasObserverPartialName(const std::filesystem::path& partialPath) {
        const auto filename = partialPath.filename().string();
        constexpr std::string_view prefix { "beammp-accepted-pose-" };
        constexpr std::string_view suffix { ".ndjson.part" };
        return filename.starts_with(prefix) && filename.ends_with(suffix) && filename.size() > prefix.size() + suffix.size();
    }

    [[nodiscard]] static bool HasSafeParentDirectory(const std::filesystem::path& partialPath) {
        const auto parent = partialPath.parent_path();
        if (parent.empty()) return false;

        std::error_code error;
        const auto absoluteParent = std::filesystem::absolute(parent, error).lexically_normal();
        if (error) return false;
        const auto resolvedParent = std::filesystem::weakly_canonical(parent, error);
        if (error || resolvedParent != absoluteParent) return false;

        const auto parentStatus = std::filesystem::symlink_status(parent, error);
        return !error && std::filesystem::is_directory(parentStatus) && !std::filesystem::is_symlink(parentStatus);
    }

    [[nodiscard]] static std::filesystem::path FinalPath(const std::filesystem::path& partialPath) {
        auto result = partialPath;
        result.replace_extension();
        return result;
    }

    std::filesystem::path mPartialPath;
    std::filesystem::path mFinalPath;
    std::uint64_t mTraceStartMonoNs {};
    TraceRecordWriter mWriter;
    std::ofstream mStream;
    std::uintmax_t mMaximumBytes {};
    std::uintmax_t mBytesWritten {};
    std::uintmax_t mFooterReserveBytes {};
    bool mOpen { false };
    bool mFinalized { false };
};

// Worker-owned lifecycle seam. Rotation always finalizes the current immutable
// epoch before opening the next one, which gives each epoch a fresh sanitizer
// and therefore fresh dense identity maps. Packet producers never use it.
class TraceEpochLifecycle final {
public:
    TraceEpochLifecycle(const TraceEpochRotationPolicy rotationPolicy, const std::size_t maxPlayers, const std::size_t maxVehicles,
        const std::uintmax_t maximumBytes = std::numeric_limits<std::uintmax_t>::max())
        : mRotationPolicy(rotationPolicy), mMaxPlayers(maxPlayers), mMaxVehicles(maxVehicles), mMaximumBytes(maximumBytes) { }

    [[nodiscard]] bool Start(const std::filesystem::path& partialPath, const std::uint64_t traceStartMonoNs) {
        if (mEpoch) return false;
        auto epoch = std::make_unique<TraceEpochFile>(partialPath, traceStartMonoNs, mMaxPlayers, mMaxVehicles, mMaximumBytes);
        if (!epoch->IsOpen()) return false;
        mEpoch = std::move(epoch);
        return true;
    }

    [[nodiscard]] bool Append(const std::string_view rawPose, const std::int32_t playerId, const std::int32_t vehicleId,
        const std::uint64_t acceptedMonoNs) {
        return mEpoch && mEpoch->Append(rawPose, playerId, vehicleId, acceptedMonoNs);
    }

    [[nodiscard]] bool RotateIfNeeded(const std::uint64_t currentMonoNs, const TraceRecordWriter::FooterMetrics& metrics,
        const std::filesystem::path& nextPartialPath) {
        if (!mEpoch || !mEpoch->ShouldRotate(mRotationPolicy, currentMonoNs)) return false;

        // Avoid creating a header-only successor if the current immutable
        // destination is already occupied. The finalization operation still
        // performs its own no-replace link to cover races after this check.
        if (!mEpoch->CanFinalize()) return false;

        // Opening the successor can fail because its name is already immutable
        // evidence or its directory is no longer safe. Check that failure before
        // finalizing the active epoch, so a failed rotation never stops capture.
        auto nextEpoch = std::make_unique<TraceEpochFile>(nextPartialPath, currentMonoNs, mMaxPlayers, mMaxVehicles, mMaximumBytes);
        if (!nextEpoch->IsOpen()) return false;
        if (!mEpoch->Finalize(metrics)) return false;
        mEpoch = std::move(nextEpoch);
        return true;
    }

    [[nodiscard]] bool Finalize(const TraceRecordWriter::FooterMetrics& metrics) {
        if (!mEpoch || !mEpoch->Finalize(metrics)) return false;
        mEpoch.reset();
        return true;
    }

    [[nodiscard]] bool IsOpen() const noexcept { return static_cast<bool>(mEpoch); }

private:
    TraceEpochRotationPolicy mRotationPolicy;
    std::size_t mMaxPlayers {};
    std::size_t mMaxVehicles {};
    std::uintmax_t mMaximumBytes {};
    std::unique_ptr<TraceEpochFile> mEpoch;
};

} // namespace beammp::observer
