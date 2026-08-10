# Observer trace-capture current local-gate revalidation — 2026-08-10T21:31:25Z

## Scope

This checkpoint used only the local source tree and local build/test artifacts. It did not inspect, modify, restart, configure, deploy to, or capture from Server4 or any public BeamMP server. No image was built or pushed. No sensitive configuration, player identity, IP address, raw packet, authentication material, or real trace content was accessed.

## Evidence

- Branch: `feat/observer-trace-capture` at `cc371fe65817cb1dd8e0b99658b570047f08a8cd` before this evidence-only commit, synchronized with `origin/feat/observer-trace-capture`.
- `git diff --check` passed. The pinned-base comparison confirmed `src/Client.cpp` and `src/TNetwork.cpp` are unchanged from `176de6b5a5e1858ddeca9705a6ca5f8a717c0ae7`; the test-only post-store change in `src/TServer.cpp` remains intentional.
- Retained compatible GCC 14.3 Release observer-OFF tree (`/tmp/beammp-observer-release-off`): `cmake --build /tmp/beammp-observer-release-off --parallel && ctest --test-dir /tmp/beammp-observer-release-off --output-on-failure` passed **1/1** in **0.65 s**.
- Retained compatible GCC 14.3 Release test-only observer-ON tree (`/tmp/beammp-observer-release-on`): full CTest passed **2/2** in **0.70 s**; focused `ctest --test-dir /tmp/beammp-observer-release-on -R '^ObserverTrace$' --output-on-failure` passed **1/1** in **0.03 s**.
- OFF `compile_commands.json` had **0** observer matches and both OFF binaries had **0** `ObserverTrace`/`beammp::observer` symbols. ON had **61** compile-database matches and **122** matching symbols across the server and test binaries.
- A current-source host-GCC-15 ThreadSanitizer producer/drain probe rebuilt from `/tmp/beammp-observer-tsan-stress.cpp` and exited **0** under `TSAN_OPTIONS=halt_on_error=1:exitcode=66`. It exercises four concurrent producers (2,000 attempts each), a concurrent drain, and capture accounting invariants.

The enabled suite still covers deterministic saturation/newest-sentinel recovery, bounded producer slot claims, concurrent producer/drain accounting, kill-switch admission, warmed producer allocation instrumentation, and all producer-visible atomic lock-free checks.

## Current gate

Release OFF/ON CMake+CTest and the pre-writer producer gates are green. The unsuppressed full Debug ASan/UBSan/LSan gate remains blocked by the independently reproduced **55,288 bytes / 611 allocations** existing `TLuaEngine`/vendored Lua/sol2 lifecycle baseline. No suppression, workaround, or observer behavior change was added. A reviewed disposition of that baseline or a clean compatible full sanitizer run is required before image build/push or any Server4 readiness/apply/deployment/capture work.
