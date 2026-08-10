# Observer trace-capture current-head full sanitizer gate — 2026-08-10

## Scope

Local source/build/test verification only at `feat/observer-trace-capture` revision `f6277c794c1dbfb9998e0f092bd908c8028d4386`. No BeamMP server, container/image, deployment, trace capture, or sensitive configuration/data was accessed or changed.

`git diff --check` passed. The pinned upstream comparison base is `176de6b5a5e1858ddeca9705a6ca5f8a717c0ae7`; the restricted base diff for `src/Client.cpp` and `src/TNetwork.cpp` passed.

## Results

The retained Debug observer-ON ASan/UBSan build tree rebuilt successfully. With `ASAN_OPTIONS=detect_leaks=0:halt_on_error=1` and `UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`, CTest passed **2/2**: `BeamMPServerUnitTests` in 1.49 s and `ObserverTrace` in 0.16 s.

With leak detection enabled, CTest was intentionally red only after `BeamMPServerUnitTests` completed **103/103 cases** and **52,828/52,828 assertions**. LeakSanitizer reproduced the established **55,288 bytes in 611 allocations** baseline. The first reported chains enter vendored Lua and existing `TLuaEngine::StateThreadData` / `EnsureStateExists`; `ObserverTrace` still passed in 0.18 s. No suppression, workaround, or observer behavior change was made.

## Gate

Observer ASan/UBSan behavior remains green, but the clean unsuppressed full LSan gate remains blocked by the reproduced Lua/sol2 lifecycle baseline. Do not build/push an image, access Server4, deploy, or capture until a separately reviewed disposition or upstream fix resolves that gate.
