# Observer trace-capture local gate revalidation — 2026-08-10T20:46:58Z

## Scope

This checkpoint exercised local source and retained local build artifacts only. It did not inspect, modify, restart, configure, deploy to, or capture from Server4 or any public BeamMP server. No image was built or pushed. No sensitive configuration, player identity, IP address, raw packet, authentication material, or real trace content was accessed.

## Evidence

- Branch: `feat/observer-trace-capture` at `ab413a1b8dc51a12b8a46506fff388f0e342708a` before this documentation-only update; it matches `origin/feat/observer-trace-capture`.
- `git diff --check` passed.
- The intentional test-only post-store hook is the only `src/TServer.cpp` difference from pinned upstream `176de6b5a5e1858ddeca9705a6ca5f8a717c0ae7`; `src/Client.cpp` and `src/TNetwork.cpp` remain unchanged.
- Pinned GCC 14.3 Release OFF tree (`/tmp/beammp-observer-release-off`): `cmake --build ... --parallel` succeeded and `ctest --output-on-failure` passed **1/1** in 0.64 s.
- Pinned GCC 14.3 Release ON tree (`/tmp/beammp-observer-release-on`): build succeeded; full CTest passed **2/2** in 0.70 s; focused `ObserverTrace` passed **1/1** in 0.03 s.
- The exercised ON suite covers deterministic saturation/newest-sentinel recovery, producer collision accounting, concurrent producer/drain behavior, atomic kill-switch behavior, warmed producer allocation instrumentation, producer atomic lock-free checks, runtime controls/finalization, and privacy-safe status behavior.

## Current gate

The required local release OFF/ON CMake+CTest and pre-writer producer gates remain green. No trace-writer addition or further hook change was made in this run.

The independently reproduced full Debug ASan/UBSan/LSan gate remains red only after all test assertions pass because LeakSanitizer reports the established pre-existing `TLuaEngine`/vendored Lua/sol2 lifecycle baseline: **55,288 bytes in 611 allocations**. The separate host GCC 15.2 OFF build remains blocked before observer compilation by the pre-existing sol2 `optional<T&>` failure in `src/Common.cpp`. No suppression, workaround, or observer behavior change was made.

Do not build/push an image or begin Server4 readiness/apply, deployment, runtime capture, or real trace collection. A reviewed disposition of the full-LSan baseline, or a clean compatible full sanitizer run, remains the next gate. No user input is required for this local evidence checkpoint.
