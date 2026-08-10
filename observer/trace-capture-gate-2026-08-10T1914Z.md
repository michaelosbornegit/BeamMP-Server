# Observer trace-capture local release/TSan gate — 2026-08-10T19:14Z

## Scope

Local source/build/test verification on `feat/observer-trace-capture` only. No BeamMP server (private or public) was accessed, inspected, configured, restarted, deployed, or used for capture. No image was built or pushed. No sensitive configuration, player identity, IP address, raw packet, authentication material, or real trace content was accessed.

## Verified revision

- BeamMP Server revision: `aaad88c68e644ed51c8ef2e269c7bede063c6a81`
- Pinned upstream comparison base: `176de6b5a5e1858ddeca9705a6ca5f8a717c0ae7`

`git diff --check` and `git diff --exit-code <base> -- src/Client.cpp src/TNetwork.cpp` both passed.

## Actual commands and results

| Gate | Result |
|---|---|
| `cmake --build /tmp/beammp-observer-off-6229849 --parallel 2` | Passed |
| `ctest --test-dir /tmp/beammp-observer-off-6229849 --output-on-failure` | **1/1 passed**, 0.65 s |
| OFF compile commands / both binary symbol tables | **0** observer matches each |
| `cmake --build /tmp/beammp-observer-on-6229849 --parallel 2` | Passed |
| `ctest --test-dir /tmp/beammp-observer-on-6229849 --output-on-failure` | **2/2 passed**, 0.70 s |
| Focused `ObserverTrace` CTest | **1/1 passed**, 0.03 s |
| ON inclusion | 63 compile-database matches; 38 server and 83 test-binary observer symbols |
| GCC 15.2 TSan producer/drain probe | Passed under `TSAN_OPTIONS=halt_on_error=1:exitcode=66` (four producers × 2,000 attempts) |

The enabled suite retains deterministic saturation/newest-sentinel, concurrent producer accounting, kill-switch, warmed producer-allocation, and producer-visible atomic lock-free coverage. The TSan probe also checked the queue accounting identities.

## Current gate

The next local gate is a clean, unsuppressed full Debug ASan/UBSan/LSan run (or a separately reviewed disposition). The known full-suite baseline remains red only after test assertions complete: **55,288 bytes in 611 allocations**, rooted in existing `TLuaEngine`/vendored Lua/sol2 lifecycle paths. No suppression or workaround was added. Do not build/push an image, access Server4, or deploy/capture until that gate and separate operational scope are resolved.
