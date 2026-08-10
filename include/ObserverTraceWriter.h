// BeamMP, the BeamNG.drive multiplayer mod.
// Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Affero General Public License as published
// by the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

// Test-build-only worker-side record serializer. Never call from packet producers.
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

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

} // namespace beammp::observer
