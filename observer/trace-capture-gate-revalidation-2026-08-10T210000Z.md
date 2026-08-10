# Observer trace-capture local gate revalidation — 2026-08-10T21:00Z

## Scope

This checkpoint exercised only local source and retained local build trees. It did not inspect, modify, restart, configure, deploy to, or capture from Server4 or any public BeamMP server. No image was built or pushed, and no sensitive configuration, player identity, IP address, raw packet, authentication material, or real trace content was accessed.

## Evidence

- Branch: `feat/observer-trace-capture` at `dd4f51d6` before this documentation-only update; clean and aligned with its remote tracking branch.
- Pinned GCC 14.3 Release OFF tree: `cmake --build /tmp/beammp-observer-release-off --parallel && ctest --test-dir /tmp/beammp-observer-release-off --output-on-failure` passed **1/1** in **0.64 s**. Its focused unit-test rerun also passed **1/1** in **0.64 s**.
- Pinned GCC 14.3 Release ON tree: `cmake --build /tmp/beammp-observer-release-on --parallel && ctest --test-dir /tmp/beammp-observer-release-on --output-on-failure` passed **2/2** in **0.70 s**. Focused `ObserverTrace` passed **1/1** in **0.03 s**.
- Fresh retained-tree Debug ASan/UBSan build completed. The full CTest run is **red** only at LeakSanitizer: doctest first reported **103/103** cases and **52,828/52,828** assertions passing, then LeakSanitizer reported **55,288 bytes in 611 allocations**. The first reported stacks originate in pre-existing `TLuaEngine.cpp`, vendored Lua, and sol2 paths; this run does not establish an observer allocation leak.
- `git diff --check` passed. Pinned-base comparison confirms `src/Client.cpp` and `src/TNetwork.cpp` remain unchanged; the test-only `src/TServer.cpp` hook is intentional.

## Current gate

The observer OFF/ON release CMake+CTest gates and focused pre-writer producer tests are green. The unsuppressed full Debug LSan run is reproducibly red at the established Lua/sol2 lifecycle baseline, despite all doctest assertions passing. Do not build or push an image, enter Server4 readiness/apply, deploy, restart a server, or collect a real trace until that full-LSan baseline has a reviewed disposition or a clean compatible full sanitizer run exists. No user input is needed for this local checkpoint.
