# Observer trace-capture local gate revalidation — 2026-08-10T20:40:02Z

## Scope

Local source/build/test verification only on `feat/observer-trace-capture` at
`53e9ff18d09f1ae822e3ee15c13693f51cfb1561`. No BeamMP server (private or
public), image, deployment, restart, configuration, player identity, IP,
packet, authentication material, or real trace was accessed or changed.

## Passed release-active gates

- Observer **OFF**: `cmake --build /tmp/beammp-observer-release-off --parallel
  2` completed; full `ctest --output-on-failure` passed **1/1** in **0.65 s**.
  Its compile database had **0** observer entries and both OFF binaries had
  **0** `ObserverTrace`/`beammp::observer` symbols.
- Test-only observer **ON**: `cmake --build
  /tmp/beammp-observer-release-on --parallel 2` completed; full CTest passed
  **2/2** in **0.70 s**, and focused `ObserverTrace` passed **1/1** in
  **0.03 s**. The ON compile database had **57** observer entries; server/test
  binaries had **37**/**85** observer symbols.
- The current-source host-GCC-15.2 producer/drain probe rebuilt with
  `-fsanitize=thread` and exited **0** under
  `TSAN_OPTIONS=halt_on_error=1:exitcode=66`. It exercised four producers,
  each submitting 2,000 records, while a consumer drained and checked queue
  accounting invariants.
- `git diff --check` and the pinned-base checks of `src/Client.cpp` and
  `src/TNetwork.cpp` passed.

The enabled suite continues to exercise deterministic saturation/newest
sentinel, producer concurrency accounting, kill-switch handoff, warmed
producer allocation, and producer-visible atomic lock-free assertions.

## Remaining local gate

A direct leak-enabled ASan/UBSan run completed all **103/103** cases and
**52,828/52,828** assertions, then LeakSanitizer reported **55,358 bytes in
612 allocations**. The leak stacks remain in pre-existing
`TLuaEngine`/vendored Lua/sol2 lifecycle paths; no observer capture symbols
appeared in the reported stack locations. This is not a clean full sanitizer
gate. No suppression, dependency change, or observer workaround was added.

Do not build/push an image, inspect/configure/restart/deploy Server4, enable
runtime capture, or capture real traffic until a reviewed resolution or a
compatible clean full sanitizer result is available.
