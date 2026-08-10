// BeamMP, the BeamNG.drive multiplayer mod.
// Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Affero General Public License as published
// by the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

// Test-build-only worker-side record serializer. Never call from packet producers.
#pragma once

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ObserverTraceCapture.h"
#include "ObserverTraceSanitizer.h"

namespace beammp::observer {

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
                && filename.starts_with("beammp-accepted-pose-") && path.extension() == ".ndjson") {
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
                && filename.starts_with("beammp-accepted-pose-") && path.extension() == ".ndjson") {
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
};

// Worker-side epoch file only. A trace remains a .part file unless a complete
// allowlisted footer has been flushed and the atomic rename succeeds.
class TraceEpochFile final {
public:
    TraceEpochFile(const std::filesystem::path& partialPath, const std::uint64_t traceStartMonoNs, const std::size_t maxPlayers, const std::size_t maxVehicles)
        : mPartialPath(partialPath), mFinalPath(FinalPath(partialPath)), mWriter(traceStartMonoNs, maxPlayers, maxVehicles) {
        if (mPartialPath.extension() != ".part" || !HasSafeParentDirectory(mPartialPath)) return;
        std::error_code statusError;
        const auto finalStatus = std::filesystem::symlink_status(mFinalPath, statusError);
        // A final trace is immutable evidence: never open a matching partial
        // when finalization could replace an existing file.
        if (std::filesystem::exists(finalStatus) || std::filesystem::is_symlink(finalStatus)
            || (statusError && statusError != std::errc::no_such_file_or_directory)) return;
        statusError.clear();
        const auto partialStatus = std::filesystem::symlink_status(mPartialPath, statusError);
        if (std::filesystem::is_symlink(partialStatus) || (statusError && statusError != std::errc::no_such_file_or_directory)) return;
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
        mStream << mWriter.Header() << '\n';
        mOpen = static_cast<bool>(mStream);
        if (!mOpen) mStream.close();
    }

    ~TraceEpochFile() { if (mStream.is_open()) mStream.close(); }

    TraceEpochFile(const TraceEpochFile&) = delete;
    TraceEpochFile& operator=(const TraceEpochFile&) = delete;

    [[nodiscard]] bool IsOpen() const noexcept { return mOpen; }

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
        if (!line) return false;
        mStream << *line << '\n';
        return static_cast<bool>(mStream);
    }

    [[nodiscard]] bool Finalize(const TraceRecordWriter::FooterMetrics& metrics) {
        if (!mOpen || mFinalized) return false;
        mStream << mWriter.Footer(metrics) << '\n';
        mStream.flush();
        if (!mStream) return false;
        mStream.close();
        std::error_code error;
        std::filesystem::rename(mPartialPath, mFinalPath, error);
        if (error) return false;
        mFinalized = true;
        mOpen = false;
        return true;
    }

private:
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
    TraceRecordWriter mWriter;
    std::ofstream mStream;
    bool mOpen { false };
    bool mFinalized { false };
};

} // namespace beammp::observer
