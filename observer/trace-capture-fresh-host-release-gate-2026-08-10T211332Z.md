# Observer trace-capture fresh-host Release gate — 2026-08-10T21:13:32Z

## Scope

Local source/build/test verification only at `feat/observer-trace-capture`. No BeamMP private or public server, container, image, deployment, runtime capture, sensitive configuration, player identity, IP address, raw packet, authentication material, or real trace was accessed or changed.

## Fresh evidence

A clean local GCC 15.2 Release configuration was created separately for each build option, using the repository vcpkg toolchain, `-Wno-template-body` for the established sol2/GCC-15 diagnostic, and a build-local `TMPDIR`.

| Gate | Command result |
|---|---|
| Observer OFF | Configure/build completed; `ctest --test-dir bin/cron-release-off --output-on-failure` passed **1/1** in **0.65 s**. |
| Observer ON | Configure/build completed; `ctest --test-dir bin/cron-release-on --output-on-failure` passed **2/2** in **0.70 s**. |
| Focused ON suite | `ctest --test-dir bin/cron-release-on -R '^ObserverTrace$' --output-on-failure` passed **1/1** in **0.03 s**. |
| OFF exclusion | `compile_commands.json` had **0** observer/macro/source matches; `nm -C` found **0** `ObserverTrace`/`beammp::observer` symbols in each OFF binary. |
| ON inclusion | Compile commands contain the test-only observer macro and the two observer test sources; `nm -C` found **38** observer symbols in the server and **83** in the test binary. |
| Source safety | `git diff --check c6cc2a9..HEAD` passed. The pinned-base comparison shows no changes to `src/Client.cpp` or `src/TNetwork.cpp`; the option-guarded `src/TServer.cpp` hook is the reviewed boundary. |

The existing project and GCC-15 warning output remains, including a false-positive-looking `-Wmismatched-new-delete` diagnostic around the observer test-only global allocation counter. It did not cause a build or test failure; this checkpoint does not claim warning-clean output.

## Gate state

The fresh Release OFF/ON CMake+CTest gate is green and confirms that normal builds exclude observer artifacts. The remaining hard local gate is still a clean unsuppressed full Debug ASan/UBSan/LSan result, or a reviewed disposition of the independently reproduced Lua/sol2 lifecycle LeakSanitizer baseline (55,288 bytes in 611 allocations after all unit assertions complete). Do not build/push an image, perform Server4 readiness/apply, deploy, enable runtime capture, or touch public-server infrastructure.
