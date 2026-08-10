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
    TraceRecordWriter(const std::uint64_t traceStartMonoNs, const std::size_t maxPlayers, const std::size_t maxVehicles)
        : mSanitizer(traceStartMonoNs, maxPlayers, maxVehicles) { }

    [[nodiscard]] std::optional<std::string> Serialize(const RawStoredPoseV1& record) {
        if (record.PayloadSize > record.RawPose.size()) return std::nullopt;
        return mSanitizer.Sanitize(record.PlayerId, record.VehicleId, record.AcceptedMonoNs,
            std::string_view(record.RawPose.data(), record.PayloadSize));
    }

private:
    TraceSanitizer mSanitizer;
};

} // namespace beammp::observer
