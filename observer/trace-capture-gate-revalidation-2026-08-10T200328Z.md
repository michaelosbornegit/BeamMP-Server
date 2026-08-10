# Observer trace-capture local gate revalidation — 2026-08-10T20:03:28Z

## Scope

This checkpoint covers only local source, build, and test work on
`feat/observer-trace-capture`. No BeamMP server (private or public) was
accessed, inspected, configured, restarted, deployed, or used for trace
capture. No image was built or pushed. No sensitive configuration, player
identity, IP address, raw packet, authentication material, or real trace was
accessed.

## Revision and source-boundary checks

- Branch: `feat/observer-trace-capture`
- Revision before this evidence-only document: `e78aebc`
- Pinned upstream comparison base: `176de6b5a5e1858ddeca9705a6ca5f8a717c0ae7`

`git diff --check` passed. The pinned-base comparison of `src/Client.cpp` and
`src/TNetwork.cpp` passed with no changes. The worktree was clean before this
document was added.

## Fresh Release-active results

The retained compatible Release build trees rebuilt successfully:

| Mode | Build | CTest |
|---|---|---|
| Observer OFF | `cmake --build /tmp/beammp-observer-release-off --parallel` | `ctest --test-dir /tmp/beammp-observer-release-off --output-on-failure`: **1/1 passed** (0.65 s) |
| Test-only observer ON | `cmake --build /tmp/beammp-observer-release-on --parallel` | `ctest --test-dir /tmp/beammp-observer-release-on --output-on-failure`: **2/2 passed** (0.70 s) |
| Focused ON suite | already-built ON tree | `ctest --test-dir /tmp/beammp-observer-release-on -R '^ObserverTrace$' --output-on-failure`: **1/1 passed** (0.03 s) |

The OFF compile database had **0** observer/test-only matches; its server and
test binaries had **0** `ObserverTrace` or `beammp::observer` symbols. The ON
compile database had **61** observer/test-only matches; its server and test
binaries had **37** and **85** observer symbols, respectively.

A freshly rebuilt host GCC 15 ThreadSanitizer producer/drain probe also passed:

```text
g++ -std=c++20 -O1 -g -fsanitize=thread -fno-omit-frame-pointer \
  -Iinclude /tmp/beammp-observer-tsan-stress.cpp -pthread \
  -o /tmp/beammp-observer-tsan-stress-current
TSAN_OPTIONS=halt_on_error=1:exitcode=66 /tmp/beammp-observer-tsan-stress-current
```

The command exited 0. The probe exercises four producers (2,000 attempts each)
with concurrent draining and accounting checks. The enabled observer suite also
covers deterministic saturation/newest retention, kill-switch behavior, warmed
producer allocations, and producer-visible atomic lock freedom.

## Gate

The full unsuppressed Debug ASan/UBSan/LSan gate remains blocked by the already
reproduced upstream lifecycle leak baseline: the complete suite finishes, then
LSan reports **55,288 bytes in 611 allocations** rooted in existing
`TLuaEngine`/vendored Lua/sol2 paths. No observer source workaround or
suppression was added. Until a reviewed baseline resolution or a clean
compatible full sanitizer result exists, do not build/push an image or perform
Server4 readiness/apply, deployment, runtime enablement, or real trace capture.
