# Observer trace-capture Release/TSan gate revalidation — 2026-08-10T21:00:24Z

## Scope

Local source/build/test verification only. No BeamMP private or public server was accessed, inspected, configured, restarted, deployed, or used for capture. No image was built or pushed. No sensitive configuration, player identity, IP address, raw packet, authentication material, or real trace was accessed.

## Evidence

- Branch: `feat/observer-trace-capture` at `222bb205a139c499985b5c9a34719aa3a8a71a4a` before this documentation-only commit.
- `cmake --build /tmp/beammp-observer-release-off --parallel` and `ctest --test-dir /tmp/beammp-observer-release-off --output-on-failure` passed: Release observer-OFF **1/1** in **0.64 s**.
- `cmake --build /tmp/beammp-observer-release-on --parallel` and `ctest --test-dir /tmp/beammp-observer-release-on --output-on-failure` passed: test-only observer-ON **2/2** in **0.70 s**. Focused `ObserverTrace` passed **1/1** in **0.03 s**.
- OFF `compile_commands.json` had **0** observer macro/source matches; `nm -C` found **0** observer symbols in each OFF binary. The ON server/test binaries contained **37**/**85** observer symbols.
- A fresh host-compiler standalone ThreadSanitizer producer/drain probe (four producers × 2,000 captures plus concurrent drain) compiled with `-fsanitize=thread` and exited **0** under `TSAN_OPTIONS=halt_on_error=1:exitcode=66`.
- `git diff --check` and the pinned-base comparison confirm `src/Client.cpp` and `src/TNetwork.cpp` remain unchanged from `176de6b5a5e1858ddeca9705a6ca5f8a717c0ae7`.

## Gate

The release-active OFF/ON CMake+CTest and local MPSC TSan gates remain green. The next hard local gate is still a clean unsuppressed full Debug ASan/UBSan/LSan result, or a reviewed disposition of the independently reproduced pre-existing Lua/sol2 lifecycle LeakSanitizer baseline (55,288 bytes in 611 allocations after unit tests complete). No suppression or observer behavior change was made. Do not build/push an image, perform Server4 readiness/apply, deploy, enable runtime capture, or touch public-server infrastructure.
