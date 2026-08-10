# Local observer trace-capture gate revalidation — 2026-08-10T21:35:19Z

This is local build/test evidence only. It does **not** authorize an image build/push, Server4 or public-server access, configuration inspection, trace capture, deployment, or restart.

- Branch: `feat/observer-trace-capture` at `140f8b42eba6757722b9841f91eb87c236f3ebc1` before this evidence commit.
- Retained compatible Release observer **OFF** build: `cmake --build . --parallel && ctest --output-on-failure` passed **1/1** in **0.65 s**.
- Retained compatible Release test-only observer **ON** build: the same command passed **2/2** in **0.70 s**; focused `ctest -R '^ObserverTrace$' --output-on-failure` passed **1/1** in **0.03 s**.
- OFF `compile_commands.json` had **0** observer macro/source entries; OFF server and test binaries had **0** `ObserverTrace`/`beammp::observer` symbols. ON had **63** compile-database matches and **37**/**85** observer symbols in the server/test binaries.
- Current-source GCC 15.2 ThreadSanitizer producer/drain stress exited **0** under `TSAN_OPTIONS=halt_on_error=1:exitcode=66`.
- `git diff --check` and the pinned-base `src/Client.cpp`/`src/TNetwork.cpp` comparison passed.

The enabled release suite retains deterministic saturation/newest-sentinel, concurrent producer accounting, kill-switch, warmed-allocation, and producer-visible atomic lock-free coverage. The next local gate remains the independently reproduced unsuppressed full-LSan baseline: **55,288 bytes in 611 allocations** rooted in existing `TLuaEngine`/vendored Lua/sol2 lifecycle code after test assertions pass. No suppression or source workaround was introduced.
