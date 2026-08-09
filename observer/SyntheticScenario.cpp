#include "SyntheticScenario.h"

#include <cmath>
#include <numbers>
#include <stdexcept>

namespace beammp::observer {

SyntheticScenario::SyntheticScenario(const SyntheticScenarioConfig config)
    : mConfig(config) {
    if (config.players == 0 || config.players > 15 || config.rate_hz == 0 || config.rate_hz > 50) {
        throw std::invalid_argument("SyntheticScenario must remain inside the approved 15-player, 50-Hz cap");
    }
}

AcceptedPose SyntheticScenario::sample(const std::uint64_t timestamp_us, const std::uint16_t player_id) const {
    if (player_id >= mConfig.players || timestamp_us == 0) {
        throw std::invalid_argument("SyntheticScenario sample is outside configured bounds");
    }

    constexpr float pi = std::numbers::pi_v<float>;
    const float seconds = static_cast<float>(timestamp_us) / 1'000'000.0F;
    const float lane_phase = static_cast<float>((mConfig.seed + player_id * 37U) % 360U) * pi / 180.0F;
    const float angular_speed = 0.25F + 0.015F * static_cast<float>(player_id % 5U);
    const float phase = seconds * angular_speed + lane_phase;
    const float radius = 35.0F + 4.0F * static_cast<float>(player_id % 4U);
    const float center_x = 120.0F * static_cast<float>(player_id % 5U);
    const float center_y = 90.0F * static_cast<float>(player_id / 5U);

    const float x = center_x + radius * std::sin(phase);
    const float y = center_y + 0.65F * radius * std::sin(phase) * std::cos(phase);
    const float vx = radius * angular_speed * std::cos(phase);
    const float vy = 0.65F * radius * angular_speed * std::cos(2.0F * phase);
    const float yaw = std::atan2(vy, vx);
    const float half_yaw = yaw * 0.5F;

    return {
        .timestamp_us = timestamp_us,
        .player_id = player_id,
        .vehicle_id = 0,
        .position = {x, y, 0.0F},
        .rotation = {0.0F, 0.0F, std::sin(half_yaw), std::cos(half_yaw)},
        .velocity = {vx, vy, 0.0F},
        .angular_velocity = {0.0F, 0.0F, angular_speed},
    };
}

} // namespace beammp::observer
