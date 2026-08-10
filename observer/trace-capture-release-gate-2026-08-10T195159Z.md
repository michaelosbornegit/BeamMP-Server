# Observer trace-capture Release gate revalidation — 2026-08-10T19:51:59Z

## Scope

Local source/build/test verification only, before this evidence commit at BeamMP
Server branch `feat/observer-trace-capture` revision
`81544a642e9a6c9ea4872c55399baecd99225947`. No server, container, image,
deployment, trace capture, public-server action, or sensitive configuration/data
was accessed or changed.

The pinned upstream `minor` remains
`176de6b5a5e1858ddeca9705a6ca5f8a717c0ae7` after `git fetch`; no source-base
drift was detected.

## Passed release-active gates

- `git diff --check` passed.
- The pinned-base comparison for `src/Client.cpp` and `src/TNetwork.cpp` passed.
- `cmake --build /tmp/beammp-observer-release-off --parallel 2` and full CTest
  passed **1/1** (0.64 s). OFF `compile_commands.json` and both OFF binaries
  had **0** `ObserverTrace`/`beammp::observer`/observer-option matches.
- `cmake --build /tmp/beammp-observer-release-on --parallel 2` and full CTest
  passed **2/2** (0.70 s). Focused `ObserverTrace` also passed **1/1** (0.03 s).

## Current blocking gate

No implementation changed in this run. The remaining local gate is the already
reproduced *unsuppressed* Debug ASan/UBSan/LSan full-suite failure: after all
unit assertions complete, LeakSanitizer reports 55,288 bytes in 611 allocations
from existing `TLuaEngine` and vendored Lua/sol2 lifecycle paths. No suppression
or observer workaround was added. Do not build/push an image, access Server4,
deploy, or capture until that baseline has a reviewed resolution.
