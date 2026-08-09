#include "SyntheticScenario.h"

#include <cassert>
#include <cmath>
#include <iostream>

int main() {
    beammp::observer::SyntheticScenario scenario({.players = 15, .rate_hz = 50, .seed = 42});

    const auto first = scenario.sample(20'000, 7);
    const auto second = scenario.sample(40'000, 7);
    assert(first.player_id == 7);
    assert(first.vehicle_id == 0);
    assert(second.timestamp_us > first.timestamp_us);
    assert(std::isfinite(first.position[0]));
    assert(std::isfinite(first.rotation[3]));
    assert(std::isfinite(first.velocity[0]));
    assert(first.position != second.position);

    std::cout << "synthetic scenario tests passed\n";
}
