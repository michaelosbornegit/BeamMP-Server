# Observer trace-capture local gate revalidation — 2026-08-10T19:55:30Z

## Scope

Local source/build/test verification only, at BeamMP Server branch
`feat/observer-trace-capture` revision
`e1f3ce21008d556eef192aaddf6b2b988477e318` before this evidence commit. No
server, container, image, deployment, real trace capture, public-server action,
or sensitive configuration/data was accessed or changed.

## Evidence

- `git fetch upstream minor` resolved the pinned upstream base to
  `176de6b5a5e1858ddeca9705a6ca5f8a717c0ae7`.
- `git diff --check` and the pinned-base comparison of `src/Client.cpp` and
  `src/TNetwork.cpp` passed.
- Release OFF: `cmake --build /tmp/beammp-observer-release-off --parallel 2`
  and full CTest passed **1/1** in **0.64 s**. Its compile database and both
  binaries had **0** observer option/source or `ObserverTrace`/
  `beammp::observer` symbol matches.
- Release test-only ON: the equivalent build and full CTest passed **2/2** in
  **0.69 s**; focused `ObserverTrace` passed **1/1** in **0.03 s**. ON had
  **59** observer compile-database matches and **37**/**85** observer symbols
  in server/test binaries.
- A newly compiled host GCC 15.2 TSan producer/drain probe (four producers ×
  2,000 records) exited **0** under
  `TSAN_OPTIONS=halt_on_error=1:exitcode=66`.

## Blocking result

The unsuppressed leak-enabled Debug ASan/UBSan/LSan full suite remains red only
at the established baseline: all **103/103** test cases and
**52,828/52,828** assertions completed, and `ObserverTrace` passed, before
LeakSanitizer reported **55,288 bytes in 611 allocations** rooted in existing
`TLuaEngine` and vendored Lua/sol2 lifecycle paths. No suppression, dependency
change, or observer workaround was added.

This full-LSan baseline remains the local blocker. Do not build/push an image,
access Server4, deploy, configure runtime capture, or capture traces until it
has a reviewed resolution.
