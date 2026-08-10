# Observer trace-capture local gate revalidation — 2026-08-10T20:34:48Z

## Scope

Local source/build/test verification only at BeamMP Server revision
`9ac03e179ee624dfdbf1d0eb43d2f3567be1ce1e` on
`feat/observer-trace-capture`. No BeamMP server (private or public), image,
deployment, restart, configuration, trace, player identity, IP address, raw
packet, or authentication material was accessed or changed.

## Passed release-active gates

- Release observer **OFF**: `cmake --build /tmp/beammp-observer-release-off
  --parallel 2` completed; full `ctest --output-on-failure` passed **1/1** in
  **0.64 s**. The OFF compile database had **0** observer entries and both OFF
  binaries had **0** `ObserverTrace`/`beammp::observer` symbols.
- Release test-only observer **ON**: `cmake --build
  /tmp/beammp-observer-release-on --parallel 2` completed; full CTest passed
  **2/2** in **0.70 s**, and focused `ObserverTrace` passed **1/1** in
  **0.03 s**. ON evidence had **63** observer-related compile-database matches
  and **37**/**85** observer symbols in server/test binaries.
- Current-source host-GCC-15.2 producer/drain stress was rebuilt with
  `-fsanitize=thread`; four producers each submitted 2,000 records while a
  consumer drained concurrently. It exited **0** under
  `TSAN_OPTIONS=halt_on_error=1:exitcode=66`, with queue accounting invariants
  holding.
- `git diff --check` and the pinned-base comparisons of `src/Client.cpp` and
  `src/TNetwork.cpp` passed.

## Remaining blocker

The unsuppressed leak-enabled Debug ASan/UBSan/LSan full suite remains blocked
by the independently reproduced pre-existing Lua/sol2 lifecycle baseline:
**55,288 bytes in 611 allocations** after all test assertions complete. No
suppression, dependency change, or observer workaround was added. A compatible
clean full sanitizer result or a reviewed disposition is required before any
image build/push, Server4 scope work, runtime trace enablement, or real capture.
