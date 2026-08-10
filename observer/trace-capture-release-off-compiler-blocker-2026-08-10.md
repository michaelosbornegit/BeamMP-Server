# Observer trace-capture fresh Release OFF gate blocker — 2026-08-10

## Scope

Local source/build verification only at `feat/observer-trace-capture` revision
`3bf3f47555c30207b9b6c9b16593d4a966ba7a1d`. No BeamMP server, container,
image, deployment, trace capture, or sensitive configuration/data was accessed
or changed.

## Checks that passed

- `git diff --check` passed.
- `git diff --exit-code 176de6b5a5e1858ddeca9705a6ca5f8a717c0ae7 -- src/Client.cpp src/TNetwork.cpp` passed.
- A fresh Release observer-ON configure/build/CTest gate passed using GCC
  `15.2.0`: `BeamMPServerUnitTests` and `ObserverTrace` passed (**2/2**, 0.70 s).

## Fresh observer-OFF failure

A fresh Release observer-OFF configure succeeded, but its build is red before
CTest. Repeating `cmake --build /tmp/beammp-observer-cron-off --parallel 2`
reproduced the same error in the unmodified vendored dependency:

```
.../sol/optional_implementation.hpp:2194:31:
error: 'class sol::optional<T&>' has no member named 'construct' [-Wtemplate-body]
```

The failure occurs while compiling existing `src/Common.cpp` through
`include/TLuaResult.h` and vcpkg `sol2` `3.3.1` under GCC `15.2.0`; it occurs
for both `BeamMP-Server` and `BeamMP-Server-tests`. It is independent of
observer sources, which are excluded when the option is OFF. CMake also emits
existing Boost-policy and toml11-deprecation warnings; those are not the build
failure.

Because the normal OFF binary never linked, this run could not perform the
required OFF CTest or compile-command/symbol-exclusion checks. No workaround,
suppression, dependency change, or observer production-code change was made.

## Gate

The full OFF/ON CMake+CTest gate is now **blocked** by the reproduced GCC
15/sol2 compatibility failure. Keep image work, Server4 preflight/application,
deployment, and real trace capture blocked. The next local action is a reviewed
resolution or pinning of the project toolchain/dependency baseline, followed by
a fresh OFF/ON rebuild and the existing unsuppressed LSan gate.
