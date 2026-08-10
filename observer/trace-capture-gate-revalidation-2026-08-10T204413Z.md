# Observer trace-capture local gate revalidation — 2026-08-10T20:44:13Z

## Scope

This checkpoint exercised local source and retained local build artifacts only. It did not inspect, modify, restart, configure, deploy to, or capture from Server4 or any public BeamMP server. No image was built or pushed.

## Evidence

- Branch: `feat/observer-trace-capture` at `28a4a288224a62decd6df2bb0ded9c85cca73836` before this documentation-only update.
- `git diff --check` passed.
- `git diff --exit-code 176de6b5a5e1858ddeca9705a6ca5f8a717c0ae7 -- src/Client.cpp src/TNetwork.cpp` passed.
- Pinned GCC 14.3 Release OFF tree (`/tmp/beammp-observer-release-off`): build succeeded; `ctest --output-on-failure` passed **1/1** in 0.64 s. Its compile database had **0** observer entries and both OFF binaries had **0** `ObserverTrace`/`beammp::observer` symbols.
- Pinned GCC 14.3 Release ON tree (`/tmp/beammp-observer-release-on`): build succeeded; full CTest passed **2/2** in 0.70 s; focused `ObserverTrace` passed **1/1** in 0.03 s. Its compile database had **63** observer-related matches and its server/test binaries had **37**/**85** observer symbols.
- A freshly compiled host-GCC-15.2 ThreadSanitizer producer/drain stress executable completed with exit 0 under `TSAN_OPTIONS=halt_on_error=1:exitcode=66`.

## Current blocker

A separate retained host-GCC-15.2 OFF build attempt remains red before observer code compilation because vcpkg sol2 3.3.1 fails in existing `src/Common.cpp` at `sol/optional_implementation.hpp:2194` (`sol::optional<T&>` has no `construct`). No BeamMP or observer source workaround was added.

The independently reproduced unsuppressed full Debug ASan/UBSan/LSan run also remains red only after all test assertions pass, due to the established **55,288 bytes in 611 allocations** LeakSanitizer report rooted in pre-existing `TLuaEngine`/vendored Lua/sol2 lifecycle paths. That clean full-LSan gate remains required before image or Server4 work.

No implementation behavior changed in this checkpoint.
