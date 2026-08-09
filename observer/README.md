# Experimental observer harness

This directory contains an **offline-only, non-production** first step for validating the planned BeamMP Observer boundary:

```text
AcceptedPose → AcceptedPoseFeed::onAcceptedPose → bounded latest-pose state
```

It does not open sockets, authenticate to BeamMP, connect to a server, modify BeamMP state, or attach to the current BeamMP packet path.

## Current benchmark profile

`accepted_pose_harness.cpp` generates deterministic, finite figure-eight-style trajectories for:

- 15 logical players;
- one logical vehicle per player;
- 50 accepted poses/sec per vehicle;
- 60 seconds / **45,000 accepted poses** total.

This is the Tier-2 observer-core benchmark described in the private project’s load-test strategy. It proves neither BeamMP socket/relay scalability nor true 15-client behavior.

## Safety boundary

- The immutable harness limit is 15 players × 1 vehicle/player.
- Invalid/non-finite and out-of-range samples are rejected before mutating state.
- The feed stores the latest pose in fixed-capacity state. It is intentionally not yet connected to production network threads; a future hook must provide explicit nonblocking concurrent publication semantics and pass the full test-server gate.
- Do not use this harness to emit BeamMP protocol packets or interact with non-test servers.

## Local verification

```bash
g++ -std=c++20 -Wall -Wextra -Werror -pedantic -Iobserver \
  observer/tests/accepted_pose_feed_test.cpp observer/AcceptedPoseFeed.cpp \
  -o /tmp/accepted_pose_feed_test && /tmp/accepted_pose_feed_test

g++ -std=c++20 -Wall -Wextra -Werror -pedantic -Iobserver \
  observer/tests/synthetic_scenario_test.cpp observer/SyntheticScenario.cpp observer/AcceptedPoseFeed.cpp \
  -o /tmp/synthetic_scenario_test && /tmp/synthetic_scenario_test

g++ -O2 -std=c++20 -Wall -Wextra -Werror -pedantic -Iobserver \
  observer/accepted_pose_harness.cpp observer/SyntheticScenario.cpp observer/AcceptedPoseFeed.cpp \
  -o /tmp/accepted_pose_harness && /tmp/accepted_pose_harness
```
