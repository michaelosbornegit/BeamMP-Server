# Observer trace-capture local gate revalidation — 2026-08-10

## Scope

Local source/build/test verification only at `feat/observer-trace-capture` revision `528bab76e96bdf737333f8dacbdeba911413bd7b`. No BeamMP server, container, image, deployment, trace capture, sensitive configuration, player identity, address, packet, or authentication data was accessed or changed.

## Verified gates

- `git diff --check` passed.
- `git diff --exit-code 176de6b5a5e1858ddeca9705a6ca5f8a717c0ae7 -- src/Client.cpp src/TNetwork.cpp` passed.
- `cmake --build /tmp/beammp-observer-release-off --parallel 2` and `ctest --test-dir /tmp/beammp-observer-release-off --output-on-failure` passed **1/1** (`BeamMPServerUnitTests`, 0.64 s). The OFF compile database had no observer entries and `nm -C BeamMP-Server` had no `ObserverTrace`, `observertrace`, or `beammp::observer` symbols.
- `cmake --build /tmp/beammp-observer-release-on --parallel 2` and `ctest --test-dir /tmp/beammp-observer-release-on --output-on-failure` passed **2/2** (`BeamMPServerUnitTests`, `ObserverTrace`, 0.69 s).
- ASan/UBSan with leak checking disabled passed **2/2** (1.17 s) using `ASAN_OPTIONS=detect_leaks=0:halt_on_error=1` and `UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`.

## Blocking full sanitizer result

The unsuppressed full LSan CTest run is red: `BeamMPServerUnitTests` completed **103/103 cases** and **52,828/52,828 assertions**, then LeakSanitizer reported the established **55,288 bytes in 611 allocations**. The reported allocation chains enter vendored Lua/sol2 and existing `TLuaEngine::StateThreadData` / `EnsureStateExists`; `ObserverTrace` still passed (0.18 s). No suppression, workaround, or observer code change was made.

## Next gate

A reviewed resolution of the upstream Lua/sol2 lifecycle leak baseline is required before image work, Server4 preflight/application, deployment, or real trace capture. No user input is needed for this local checkpoint; scope expansion would require explicit approval.
