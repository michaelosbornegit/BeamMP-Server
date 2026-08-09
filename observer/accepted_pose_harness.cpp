// Offline-only benchmark. It never opens sockets or contacts BeamMP services.
#include "AcceptedPoseFeed.h"
#include "SyntheticScenario.h"

#include <chrono>
#include <cstdint>
#include <iostream>

int main() {
    constexpr std::uint16_t players = 15;
    constexpr std::uint16_t rate_hz = 50;
    constexpr std::uint32_t seconds = 60;
    constexpr std::uint64_t step_us = 1'000'000 / rate_hz;

    beammp::observer::AcceptedPoseFeed feed({.max_players = players, .max_vehicles_per_player = 1});
    beammp::observer::SyntheticScenario scenario({.players = players, .rate_hz = rate_hz, .seed = 42});

    const auto started = std::chrono::steady_clock::now();
    for (std::uint32_t tick = 1; tick <= seconds * rate_hz; ++tick) {
        for (std::uint16_t player = 0; player < players; ++player) {
            const auto result = feed.onAcceptedPose(scenario.sample(static_cast<std::uint64_t>(tick) * step_us, player));
            if (result != beammp::observer::FeedResult::accepted && result != beammp::observer::FeedResult::replaced) {
                std::cerr << "unexpected rejected synthetic sample\n";
                return 1;
            }
        }
    }
    const auto elapsed = std::chrono::steady_clock::now() - started;
    const auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
    const auto& metrics = feed.metrics();

    std::cout << "mode=offline-synthetic\n"
              << "players=" << players << "\n"
              << "vehicles_per_player=1\n"
              << "input_rate_hz_per_vehicle=" << rate_hz << "\n"
              << "accepted_poses=" << metrics.accepted << "\n"
              << "replaced_poses=" << metrics.replaced << "\n"
              << "active_entities=" << feed.active_entities() << "\n"
              << "elapsed_us=" << elapsed_us << "\n";
    return metrics.accepted == static_cast<std::uint64_t>(players) * rate_hz * seconds ? 0 : 1;
}
