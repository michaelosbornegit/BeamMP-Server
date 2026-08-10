# Observer trace-capture local OFF/ON recovery — 2026-08-10T19:47:40Z

Local source/build/test verification at `feat/observer-trace-capture`
`2a2e44610cbb6a7c7427c69bb150dde53a578866` only. No server, container,
image, deployment, trace capture, or sensitive configuration/data was accessed
or changed.

Safety checks passed:

- `git diff --check`
- `git diff --exit-code 176de6b5a5e1858ddeca9705a6ca5f8a717c0ae7 -- src/Client.cpp src/TNetwork.cpp`

Retained Release gates were rebuilt on the current source:

- Observer **ON**: build passed; full CTest **2/2** in 0.70 s; focused
  `ObserverTrace` **1/1** in 0.03 s.
- Observer **OFF**: build passed; full CTest **1/1** in 0.64 s. Its compile
  database had zero observer entries, and both OFF binaries had zero observer
  symbols. ON had 57 compile-database matches and 37/85 server/test symbols.
- Debug ASan/UBSan with `detect_leaks=0` passed **2/2** in 1.16 s.

The unsuppressed full Debug LSan gate remains red: all 103 unit cases and
52,828 assertions passed and `ObserverTrace` passed, but LeakSanitizer reported
55,288 bytes in 611 allocations rooted in existing `TLuaEngine` and vendored
Lua/sol2 lifecycle paths. No observer allocation appears in its stack, and no
suppression or code workaround was added.

The OFF/ON CMake+CTest boundary is therefore green again, but the full LSan
baseline remains a hard blocker. Do not build/push an image, undertake Server4
work, deploy, or capture real traces. The next gate is a reviewed baseline-leak
disposition or upstream lifecycle fix followed by a fresh full sanitizer run.
