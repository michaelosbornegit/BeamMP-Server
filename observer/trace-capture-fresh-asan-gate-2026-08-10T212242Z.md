# Observer trace-capture fresh Debug ASan/UBSan/LSan gate — 2026-08-10T21:22:42Z

## Scope

Local source/build/test verification only on `feat/observer-trace-capture`, at source revision `4121415df1cdc8b2c96bb7d172e8bf0b5b0b4d37` before this evidence-only commit. No BeamMP server (private or public), container, image, deployment, runtime capture, sensitive configuration, player identity, IP address, raw packet, authentication material, or real trace was accessed or changed.

## Fresh evidence

A new Debug observer-ON CMake tree was configured with host GCC 15.2, the repository vcpkg toolchain, shared already-resolved local dependencies, `BeamMP-Server_ENABLE_SANITIZER=ON`, and `BEAMMP_OBSERVER_TRACE_TEST_ONLY=ON`. Temporary compiler files used a build-local directory rather than `/tmp`.

| Gate | Command result |
|---|---|
| Debug ASan/UBSan build | Configure and `cmake --build bin/cron-asan-on --parallel 2` completed. Existing project warnings remain; no warning-clean claim is made. |
| Full ASan/UBSan behavioral suite | `ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 ctest --test-dir bin/cron-asan-on --output-on-failure` passed **2/2** in **1.79 s**. |
| Observer leak-enabled suite | `ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 ctest --test-dir bin/cron-asan-on -R '^ObserverTrace$' --output-on-failure` passed **1/1** in **0.24 s**. |
| Full leak-enabled suite | The same options against all CTest tests were **red**: all **103/103** cases and **52,828/52,828** assertions passed, and `ObserverTrace` passed, but `BeamMPServerUnitTests` exited nonzero after LeakSanitizer reported **55,358 bytes in 612 allocations**. |
| Source safety | `git diff --check c6cc2a9..HEAD` passed. |

The first leak stacks originate in pre-existing `TLuaEngine::StateThreadData` / `EnsureStateExists`, vendored Lua, and sol2. This host-toolchain run differs by 70 bytes / one allocation from the earlier GCC-14.3 baseline (55,288 bytes / 611 allocations), so it is recorded as a fresh pre-existing lifecycle leak report rather than treated as a clean gate or attributed to observer code.

## Gate state

The observer-specific leak-enabled sanitizer suite and the full ASan/UBSan suite with leak reporting disabled are green. The required **unsuppressed full Debug ASan/UBSan/LSan** gate remains red because of the Lua/sol2 lifecycle leak. No suppression or source workaround was made. This blocker prevents an image build/push, Server4 readiness/apply, deployment, runtime capture, or public-server work pending a reviewed disposition or a clean compatible-toolchain full-LSan result.
