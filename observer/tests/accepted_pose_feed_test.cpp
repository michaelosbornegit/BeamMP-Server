#include "AcceptedPoseFeed.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>

namespace {

beammp::observer::AcceptedPose pose(std::uint64_t timestamp_us, std::uint16_t player, std::uint16_t vehicle, float x) {
    return {
        .timestamp_us = timestamp_us,
        .player_id = player,
        .vehicle_id = vehicle,
        .position = {x, 2.0F, 3.0F},
        .rotation = {0.0F, 0.0F, 0.0F, 1.0F},
        .velocity = {10.0F, 0.0F, 0.0F},
        .angular_velocity = {0.0F, 0.0F, 0.1F},
    };
}

void accepts_and_replaces_the_latest_pose_per_entity() {
    beammp::observer::AcceptedPoseFeed feed({.max_players = 15, .max_vehicles_per_player = 1});

    assert(feed.onAcceptedPose(pose(20'000, 3, 0, 10.0F)) == beammp::observer::FeedResult::accepted);
    assert(feed.onAcceptedPose(pose(40'000, 3, 0, 15.0F)) == beammp::observer::FeedResult::replaced);

    const auto latest = feed.latest(3, 0);
    assert(latest.has_value());
    assert(latest->timestamp_us == 40'000);
    assert(latest->position[0] == 15.0F);
    assert(feed.metrics().accepted == 2);
    assert(feed.metrics().replaced == 1);
}

void rejects_invalid_or_out_of_range_samples_without_mutating_state() {
    beammp::observer::AcceptedPoseFeed feed({.max_players = 15, .max_vehicles_per_player = 1});

    auto bad_number = pose(20'000, 0, 0, 1.0F);
    bad_number.position[1] = __builtin_nanf("");
    assert(feed.onAcceptedPose(bad_number) == beammp::observer::FeedResult::rejected_invalid);
    assert(feed.onAcceptedPose(pose(40'000, 15, 0, 1.0F)) == beammp::observer::FeedResult::rejected_out_of_range);
    assert(feed.onAcceptedPose(pose(60'000, 0, 1, 1.0F)) == beammp::observer::FeedResult::rejected_out_of_range);
    assert(!feed.latest(0, 0).has_value());
    assert(feed.metrics().rejected_invalid == 1);
    assert(feed.metrics().rejected_out_of_range == 2);
}

void sustains_the_fifteen_player_fifty_hz_target_with_bounded_state() {
    beammp::observer::AcceptedPoseFeed feed({.max_players = 15, .max_vehicles_per_player = 1});

    constexpr std::uint64_t step_us = 20'000;
    constexpr std::uint16_t players = 15;
    constexpr std::uint32_t ticks = 50;
    for (std::uint32_t tick = 0; tick < ticks; ++tick) {
        for (std::uint16_t player = 0; player < players; ++player) {
            const auto result = feed.onAcceptedPose(pose((tick + 1) * step_us, player, 0, static_cast<float>(tick + player)));
            assert(result == beammp::observer::FeedResult::accepted || result == beammp::observer::FeedResult::replaced);
        }
    }

    assert(feed.metrics().accepted == 750);
    assert(feed.active_entities() == 15);
    for (std::uint16_t player = 0; player < players; ++player) {
        const auto latest = feed.latest(player, 0);
        assert(latest.has_value());
        assert(latest->timestamp_us == ticks * step_us);
    }
}

} // namespace

int main() {
    accepts_and_replaces_the_latest_pose_per_entity();
    rejects_invalid_or_out_of_range_samples_without_mutating_state();
    sustains_the_fifteen_player_fifty_hz_target_with_bounded_state();
    std::cout << "accepted-pose feed tests passed\n";
}
