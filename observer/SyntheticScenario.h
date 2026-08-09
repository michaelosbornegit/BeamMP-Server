#pragma once

#include "AcceptedPoseFeed.h"

#include <cstdint>

namespace beammp::observer {

struct SyntheticScenarioConfig {
    std::uint16_t players;
    std::uint16_t rate_hz;
    std::uint32_t seed;
};

class SyntheticScenario final {
public:
    explicit SyntheticScenario(SyntheticScenarioConfig config);
    [[nodiscard]] AcceptedPose sample(std::uint64_t timestamp_us, std::uint16_t player_id) const;

private:
    SyntheticScenarioConfig mConfig;
};

} // namespace beammp::observer
