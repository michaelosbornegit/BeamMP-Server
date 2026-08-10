# Test-only accepted-pose trace-capture implementation boundary

**Status:** prerequisite gate only — no writer, post-store hook, runtime command, server configuration, image, or deployment is authorized by this document.

## Pinned implementation base

| Item | Value |
|---|---|
| Upstream `minor` and merge base | `176de6b5a5e1858ddeca9705a6ca5f8a717c0ae7` (BeamMP Server 3.9.3) |
| Observer implementation branch | `feat/observer-trace-capture` |
| Branch checkpoint at creation | `3daa9f04a57dec6e8e04d59c3f0d0c6368e32f72` |
| Upstream `minor` remote verification | still `176de6b5a5e1858ddeca9705a6ca5f8a717c0ae7` on 2026-08-09 |
| Private observer documentation base | `beammp-observer` `main` at `624ac45` |

The branch has not modified `src/TServer.cpp`, `src/Client.cpp`, or `src/TNetwork.cpp` relative to the pinned base. Any future hook work must re-check this baseline, record the exact tested compiler/image provenance, and stop if the intended isolated test server cannot be mapped to this source. No Server4 state was inspected for this checkpoint.

## Non-negotiable producer boundary

The only candidate hook is immediately **after** `TClient::SetCarPosition` returns in `TServer::HandlePosition`. It must pass authenticated `c.GetID()`, the parsed vehicle ID, the already-extracted raw pose suffix, and a post-store monotonic timestamp. It must be compiled only under `BEAMMP_OBSERVER_TRACE_TEST_ONLY` and excluded by default.

Producer calls must be `noexcept`, bounded to one queue push, at most one eviction pop, and one retry push. They must add no heap allocation, locks, waits, I/O, JSON parsing, logging, formatting, network activity, callbacks, or packet/store mutation. The runtime default is disabled; its kill switch is a release-store to the producer-visible atomic.

The producer record contains only monotonic timing, dense-independent numeric raw IDs, payload length, and at most 1,024 bytes of the pose suffix. Oversize/invalid records drop without changing BeamMP behavior. The queue is fixed-capacity MPSC and must be proven lock-free together with every producer-visible atomic on the target image; no mutex fallback is permitted.

## Privacy and writer constraints

A future worker is the sole parser/writer. It may emit only relative monotonic time, trace-local dense player/vehicle IDs, and finite allowlisted `pos[3]`, `rot[4]`, `vel[3]`, `rvel[3]`, plus optional finite `tim`. It must not preserve raw IDs, names, keys, IPs, authentication material, chat, roles, admin/config values, arbitrary JSON, raw packets, or file paths in status/error output.

The worker must reset bounded identity maps per trace epoch, produce a valid `.ndjson` only after finalization, and keep raw partials separate. It has no authorization to upload traces or access public servers.

## Required blocker gate before writer or hook

All of the following must pass on a release-active target before a writer or `HandlePosition` change is started:

1. deterministic capacity saturation/newest-sentinel coverage, multi-producer accounting, and concurrent kill-switch coverage;
2. zero warmed producer allocations and lock-free queue plus all producer atomics;
3. complete CMake configure/build/CTest gates with the option both `OFF` and `ON`, including proof that the OFF build has no observer compilation unit/symbol; and
4. a ThreadSanitizer race run.

On 2026-08-09, fresh local Release `OFF` and `ON` CMake/build gates passed with a temporary user-owned GCC 14.3 extraction and separately resolved vcpkg dependencies. OFF emitted no observer compile commands and registered no tests; ON built both targets and passed the combined `ObserverTrace` CTest suite. The host GCC 15.2/sol2 incompatibility was not patched or worked around in BeamMP source.

**2026-08-09 current-host recheck:** fresh Release `OFF` and `ON` configure steps completed with the repository vcpkg toolchain, but both full builds failed before any observer source compiled. GCC 15.2 rejects vendored `sol2` 3.3.1 at `sol/optional_implementation.hpp:2194` (`sol::optional<T&>` has no member `construct`), while compiling pre-existing `src/Common.cpp` (and the ON test build also reached pre-existing `src/LuaAPI.cpp`). Consequently neither CTest gate was run on this host. This is an upstream/dependency compiler-compatibility blocker, not an observer regression: the hook boundary remains clean relative to `minor`, and no BeamMP source workaround was introduced. A compatible, pinned compiler/image must re-establish the complete OFF/ON release gate before any writer or `HandlePosition` work resumes.

The original Boost fixed-size queue was rejected after ThreadSanitizer reported a concurrent freelist race. The test-only boundary now uses the source-reviewed fixed-slot `FixedMpscLatestQueue`; a GCC 15.2 `-fsanitize=thread` run of all capture tests passed cleanly. The remaining deterministic kill-switch handoff test also passed: a producer released only after the disabling release-store was rejected while the already-published record remained pending. Together with the previously passing release allocation/lock-free and OFF/ON CMake/CTest gates, the prerequisite gate is complete locally. The first worker-side vertical slice, `TraceRecordWriter`, serializes only a validated queued `RawStoredPoseV1` through the existing allowlist and has no filesystem, worker thread, hook, command, or runtime enablement path. This does not authorize deployment or public/server configuration access; subsequent work remains worker-side lifecycle/file-safety tests written first.

**2026-08-09 release-gate recheck:** with the user-owned GCC 14.3 toolchain, fresh-current-source Release builds completed in `/tmp/beammp-observer-release-off` and `/tmp/beammp-observer-release-on`. `ctest --output-on-failure` passed **1/1** OFF and **2/2** ON (including the dedicated `ObserverTrace` suite); OFF `compile_commands.json` had **0** observer entries and ON had the two dedicated observer test translation units. A GCC 15.2 ThreadSanitizer stress executable for the fixed-slot capture boundary also exited cleanly. During this checkpoint, a test was first observed failing because a rejected player-cap record had already consumed a vehicle dense ID; the sanitizer now preflights both bounded maps before either mutation, and the focused test plus both release gates pass. No post-store hook, runtime command, server configuration, image, trace capture, or deployment was added.

**2026-08-09 exact-finalized-name recheck:** worker retention now accepts only nonempty `beammp-accepted-pose-*.ndjson` basenames, matching `.part` admission rather than treating `beammp-accepted-pose-.ndjson` as a managed trace. The focused Release-active doctest was RED first (the malformed filename was deleted: three failed assertions), then GREEN after the shared filename predicate was applied to both age and total-byte retention paths. The direct Release observer suite passed **33 cases / 8,378 assertions**. Fresh GCC 14.3 Release CMake/CTest gates passed: OFF **1/1**, ON **2/2**; OFF compile commands and binary contain no observer compilation unit/symbol. This remains worker-side filesystem containment only: no hook, writer thread, runtime control, image, Server4/public-server action, configuration access, deployment, or trace capture occurred.

**2026-08-09 rotation-finalization recheck:** a new Release-active lifecycle test was RED first: when an immutable finalized name appeared after the active epoch opened, rotation created a header-only successor `.part` before its active finalization failed. `TraceEpochLifecycle` now preflights that current immutable destination before opening a successor while retaining the final no-replace hard-link check for later races. Focused doctest passed (1 case, 5 assertions); fresh-current Release CTest passed OFF **1/1** and ON **2/2**, with OFF observer compile-command/symbol exclusion verified. This is worker-side file-lifecycle containment only; no hook, runtime command, server configuration, image, trace capture, or deployment was added.

**2026-08-09 post-store boundary checkpoint:** after the prerequisite gates were re-run, the option-guarded `TServer` owns the fixed capture queue on the heap during startup (rather than placing it in the stack-resident server object). `HandlePosition` now invokes the `noexcept` capture boundary immediately after `SetCarPosition` returns, with `c.GetID()`, parsed VID, the extracted pose suffix, and a post-store `steady_clock` timestamp. The focused Release doctest was RED first because the test-only integration seam did not exist; it then passed **1 case / 6 assertions**, proving the stored suffix is captured under authenticated client ID `77` rather than text PID `999`, and a malformed suffix emits nothing. Full fresh-current GCC 14.3 Release CMake/CTest gates passed: OFF **1/1** and ON **2/2**, including both server binaries; OFF compile commands and symbols contain no observer code. No runtime enablement/configuration, worker thread, file capture, image, Server4/public-server activity, deployment, or sensitive configuration access occurred.

**2026-08-10 startup bridge checkpoint:** the test-only `TServer` constructor now reads only the six allowlisted observer environment variables through the existing complete-configuration parser. A valid enabled configuration creates the heap-owned runtime and worker before any `TNetwork` producer can be constructed; invalid, incomplete, absent, or lock-free-ineligible configuration remains disabled without a fallback queue. The new focused Release doctest was RED first on the missing runtime-observability seam, then GREEN (**1 case / 14 assertions**): it proves a server constructed from a complete dedicated-directory environment is running before a post-store pose is accepted and can finalize after the kill-switch stop. Full Release gates on the pinned GCC 14.3 toolchain passed: OFF **1/1**, ON **2/2** (including `ObserverTrace`). This is local test-only source work only: no console command, public/Server4 configuration access, image action, deployment, or real trace capture occurred.

**2026-08-10 status-counter checkpoint:** the test-only `observertrace status` boundary now reports aggregate `invalid_id`, `invalid_payload`, and `oversize` producer rejections in addition to admission, pending, eviction, and contention counters. It deliberately contains no raw payload, identifiers, path, or configuration values. The focused Release doctest was RED first (all three required fields absent), then GREEN (**1 case / 7 assertions**); the complete observer suite passed **65 cases / 8,602 assertions**. Fresh incremental Release CTest gates passed on the pinned GCC 14.3 toolchain: OFF **1/1**, ON **2/2**. OFF compile commands and symbols still contain no observer code. This remains local test-only source work: no public/Server4 action, sensitive configuration access, image build/push, deployment, or real trace capture occurred.

**2026-08-10 release-gate checkpoint:** the current `feat/observer-trace-capture` source rebuilt cleanly in the existing pinned GCC 14.3 Release build trees. CTest passed OFF **1/1** and ON **2/2**; the direct Release-active observer doctest passed **65 cases / 8,602 assertions**. The OFF compile database contained **0** test-only observer macro uses and **0** observer/accepted-pose translation units, while the OFF server binary contained **0** `beammp::observer`/`ObserverTrace` symbols; the ON build contained **9** observer translation-unit entries and **37** matching symbols. Two attempted fresh configurations were abandoned before compilation because their separate vcpkg installs exhausted the local `/tmp` quota; those incomplete directories were removed. This checkpoint adds no implementation and does not change deployment authorization: Server4/public servers, images, configurations, and real captures remain untouched.

## Scope

This work remains limited to local source/test verification. It does not authorize any public-server action, Server4 inspection/configuration/restart, container/image build or push, deployment, trace capture, or sensitive-config access. A separately approved, documented Server4 manifest is required after all local gates pass.
