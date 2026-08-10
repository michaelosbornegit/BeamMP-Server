# Observer trace-capture Release/TSan revalidation — 2026-08-10T21:27:31Z

## Scope

Local source/build/test verification only, at `3ac4d38ed8d10e0cf8370fc3173e54f57d816292` on `feat/observer-trace-capture`. No BeamMP server (private or public), container, image, deployment, runtime capture, sensitive configuration, player identity, IP address, raw packet, authentication material, or real trace was accessed or changed.

## Fresh evidence

- Retained compatible Release observer-**OFF** tree rebuilt successfully. `ctest --test-dir bin/cron-release-off --output-on-failure` passed **1/1** in **0.65 s**. Its `compile_commands.json` contained **0** observer/macro/source matches and `nm -C` over both OFF binaries contained **0** `ObserverTrace`/`beammp::observer` symbols.
- Retained compatible Release observer-**ON** tree rebuilt successfully. Full CTest passed **2/2** in **0.70 s**; the focused `ObserverTrace` gate passed **1/1** in **0.03 s**. The ON compile database contained **63** observer-related matches and the server/test binaries contained **121** matching symbols combined, as expected for the opt-in test build.
- Current-source host GCC 15.2 ThreadSanitizer producer/drain stress was freshly compiled and run with `TSAN_OPTIONS=halt_on_error=1:exitcode=66`; it exited **0**. No ThreadSanitizer diagnostic was emitted.
- `git diff --check` and the pinned-base comparison for `src/Client.cpp` and `src/TNetwork.cpp` passed.

## Gate state

The deterministic saturation/concurrency/kill-switch, warmed-allocation, and producer-visible lock-free checks remain exercised by the enabled `ObserverTrace` suite. The observer-OFF/ON CMake+CTest and TSan local prerequisites remain green.

The next gate remains an unsuppressed full Debug ASan/UBSan/LSan run with a reviewed disposition for the established upstream Lua/sol2 lifecycle leak. The latest fresh sanitizer evidence completed all behavioral tests and observer tests, then reported LeakSanitizer allocations rooted in pre-existing `TLuaEngine`/vendored Lua/sol2 lifecycle paths. Until that gate is clean or explicitly reviewed, do not build/push an image or begin Server4 readiness/apply, deployment, runtime capture, or public-server work. No user input is needed for this revalidation; a reviewed disposition is needed to clear the remaining local blocker.
