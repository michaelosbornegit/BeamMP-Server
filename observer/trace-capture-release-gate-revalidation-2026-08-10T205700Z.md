# Observer trace-capture Release gate revalidation — 2026-08-10T20:57:00Z

## Scope

This checkpoint used only local source and retained local build trees. No BeamMP server (private or public) was accessed, inspected, configured, restarted, deployed, or used for capture. No image was built or pushed. No sensitive configuration, player identity, IP address, raw packet, authentication material, or real trace content was accessed.

## Evidence

- Branch: `feat/observer-trace-capture` at `72aa7a32c1f0544a1f8f122f42e8d3e0c3c8cc78` before this documentation-only commit.
- `git diff --check` passed. The pinned source comparison against `176de6b5a5e1858ddeca9705a6ca5f8a717c0ae7` confirms `src/Client.cpp` and `src/TNetwork.cpp` remain unchanged; the reviewed, test-only post-store change in `src/TServer.cpp` remains intentional.
- `cmake --build /tmp/beammp-observer-release-off --parallel && ctest --test-dir /tmp/beammp-observer-release-off --output-on-failure` passed: Release observer-OFF CTest **1/1** in **0.65 s**.
- `cmake --build /tmp/beammp-observer-release-on --parallel && ctest --test-dir /tmp/beammp-observer-release-on --output-on-failure` passed: Release test-only observer-ON CTest **2/2** in **0.70 s**.
- `ctest --test-dir /tmp/beammp-observer-release-on -R '^ObserverTrace$' --output-on-failure` passed: focused observer suite **1/1** in **0.03 s**.

## Gate

The release-active OFF/ON CMake+CTest gate remains green. The required unsuppressed full Debug ASan/UBSan/LSan gate is still blocked by the independently reproduced pre-existing Lua/sol2 lifecycle LeakSanitizer baseline (55,288 bytes in 611 allocations after unit tests complete). No suppression, workaround, or observer behavior change was made. Do not build/push an image or perform Server4 readiness/apply, deployment, runtime capture, or public-server work until that gate has a reviewed disposition or a clean compatible sanitizer run.