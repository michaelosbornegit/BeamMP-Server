# Observer trace-capture local gate revalidation — 2026-08-10T20:26:48Z

**Revision tested:** `30de90660f17f3eff79e706a2046c61f14391998` on `feat/observer-trace-capture`.

## Scope

Local source/build/test verification only. No BeamMP server (private or public), image, deployment, restart, configuration, real trace, player identity, IP address, packet, or authentication material was accessed or changed.

## Evidence

| Gate | Result |
|---|---|
| Release observer OFF build + CTest | Passed: `BeamMPServerUnitTests` 1/1 (0.64 s) |
| Release test-only observer ON build + CTest | Passed: 2/2 (0.70 s), including `ObserverTrace` |
| Focused Release observer suite | Passed: 1/1 (0.03 s) |
| OFF build exclusion | `compile_commands.json`: 0 observer entries; both OFF binaries: 0 `ObserverTrace`/`beammp::observer` symbols |
| ON linkage evidence | 122 combined observer symbols in server and test binaries |
| TSan producer/drain probe | Passed under `TSAN_OPTIONS=halt_on_error=1:exitcode=66`; four producers × 2,000 attempts plus concurrent drain; accounting invariants held |
| Source containment | `git diff --check` and pinned-base `Client.cpp`/`TNetwork.cpp` comparison passed |

## Current gate

The deterministic saturation/concurrency/kill-switch, warmed-allocation, producer-atomic lock-free, OFF/ON CMake+CTest, and TSan prerequisites remain green. The hard local blocker before any image build/push or Server4 scope is still the independently reproduced, unsuppressed full-LSan baseline: 55,288 bytes in 611 allocations rooted in pre-existing `TLuaEngine`/vendored Lua/sol2 lifecycle paths. No suppression or observer-source workaround was added.
