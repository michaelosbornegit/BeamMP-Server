#include "AcceptedPoseFeed.h"

#include <cmath>
#include <stdexcept>

namespace beammp::observer {

AcceptedPoseFeed::AcceptedPoseFeed(const FeedLimits limits)
    : mLimits(limits) {
    if (limits.max_players == 0 || limits.max_players > kHardMaxPlayers || limits.max_vehicles_per_player == 0 || limits.max_vehicles_per_player > kHardMaxVehiclesPerPlayer) {
        throw std::invalid_argument("AcceptedPoseFeed limits must fit the immutable benchmark cap");
    }
}

FeedResult AcceptedPoseFeed::onAcceptedPose(const AcceptedPose& sample) {
    if (sample.player_id >= mLimits.max_players || sample.vehicle_id >= mLimits.max_vehicles_per_player) {
        ++mMetrics.rejected_out_of_range;
        return FeedResult::rejected_out_of_range;
    }
    if (!valid(sample)) {
        ++mMetrics.rejected_invalid;
        return FeedResult::rejected_invalid;
    }

    auto& slot = mLatest[index(sample.player_id, sample.vehicle_id)];
    const bool replaced = slot.has_value();
    slot = sample;
    ++mMetrics.accepted;
    if (replaced) {
        ++mMetrics.replaced;
        return FeedResult::replaced;
    }
    ++mActiveEntities;
    return FeedResult::accepted;
}

std::optional<AcceptedPose> AcceptedPoseFeed::latest(const std::uint16_t player_id, const std::uint16_t vehicle_id) const {
    if (player_id >= mLimits.max_players || vehicle_id >= mLimits.max_vehicles_per_player) {
        return std::nullopt;
    }
    return mLatest[index(player_id, vehicle_id)];
}

std::size_t AcceptedPoseFeed::active_entities() const noexcept {
    return mActiveEntities;
}

const FeedMetrics& AcceptedPoseFeed::metrics() const noexcept {
    return mMetrics;
}

bool AcceptedPoseFeed::valid(const AcceptedPose& sample) const noexcept {
    const auto finite = [](const auto& values) {
        for (const float value : values) {
            if (!std::isfinite(value)) {
                return false;
            }
        }
        return true;
    };
    return sample.timestamp_us > 0 && finite(sample.position) && finite(sample.rotation) && finite(sample.velocity) && finite(sample.angular_velocity);
}

std::size_t AcceptedPoseFeed::index(const std::uint16_t player_id, const std::uint16_t vehicle_id) const noexcept {
    return static_cast<std::size_t>(player_id) * mLimits.max_vehicles_per_player + vehicle_id;
}

} // namespace beammp::observer
