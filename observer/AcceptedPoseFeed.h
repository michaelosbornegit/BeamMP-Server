// Experimental observer benchmark component. Not wired into BeamMP packet paths.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace beammp::observer {

struct AcceptedPose {
    std::uint64_t timestamp_us;
    std::uint16_t player_id;
    std::uint16_t vehicle_id;
    std::array<float, 3> position;
    std::array<float, 4> rotation;
    std::array<float, 3> velocity;
    std::array<float, 3> angular_velocity;
};

struct FeedLimits {
    std::uint16_t max_players;
    std::uint16_t max_vehicles_per_player;
};

enum class FeedResult {
    accepted,
    replaced,
    rejected_invalid,
    rejected_out_of_range,
};

struct FeedMetrics {
    std::uint64_t accepted = 0;
    std::uint64_t replaced = 0;
    std::uint64_t rejected_invalid = 0;
    std::uint64_t rejected_out_of_range = 0;
};

class AcceptedPoseFeed final {
public:
    explicit AcceptedPoseFeed(FeedLimits limits);

    FeedResult onAcceptedPose(const AcceptedPose& sample);
    [[nodiscard]] std::optional<AcceptedPose> latest(std::uint16_t player_id, std::uint16_t vehicle_id) const;
    [[nodiscard]] std::size_t active_entities() const noexcept;
    [[nodiscard]] const FeedMetrics& metrics() const noexcept;

private:
    static constexpr std::size_t kHardMaxPlayers = 15;
    static constexpr std::size_t kHardMaxVehiclesPerPlayer = 1;
    static constexpr std::size_t kCapacity = kHardMaxPlayers * kHardMaxVehiclesPerPlayer;

    [[nodiscard]] bool valid(const AcceptedPose& sample) const noexcept;
    [[nodiscard]] std::size_t index(std::uint16_t player_id, std::uint16_t vehicle_id) const noexcept;

    FeedLimits mLimits;
    std::array<std::optional<AcceptedPose>, kCapacity> mLatest{};
    FeedMetrics mMetrics{};
    std::size_t mActiveEntities = 0;
};

} // namespace beammp::observer
