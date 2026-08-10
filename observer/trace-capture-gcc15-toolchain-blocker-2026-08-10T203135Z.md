# Observer trace-capture GCC 15 toolchain blocker — 2026-08-10T20:31:35Z

**Revision:** `c82b5520580608e2b1858e723e4570cabaacbb94` on
`feat/observer-trace-capture`.

## Scope

Local source/build verification only. No BeamMP server (private or public),
image, deployment, restart, configuration, trace, player identity, IP address,
packet, or authentication material was accessed or changed.

## Fresh attempt

A fresh observer-OFF Release configure used the repository vcpkg toolchain and
host `g++ 15.2.0`:

```sh
cmake -S . -B build-cron-off-release \
  -DCMAKE_BUILD_TYPE=Release \
  -DBeamMP-Server_ENABLE_UNIT_TESTING=ON \
  -DBEAMMP_OBSERVER_TRACE_TEST_ONLY=OFF \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/vcpkg/scripts/buildsystems/vcpkg.cmake"
cmake --build build-cron-off-release --parallel 2
```

Configuration completed. The build failed before any observer source is
compiled, in pre-existing vendored `sol2` code included from `src/Common.cpp`:

```text
sol/optional_implementation.hpp:2194:31: error:
'class sol::optional<T&>' has no member named 'construct' [-Wtemplate-body]
```

The same error occurred for both `BeamMP-Server` and `BeamMP-Server-tests`.
The host no longer has the previously compatible `g++-14` executable, so the
Release OFF/ON CTest matrix was not runnable in this fresh GCC 15.2 tree.

## Unchanged evidence and gate

Before this toolchain-only recheck, the latest compatible local revalidation at
`30de90660f17f3eff79e706a2046c61f14391998` recorded Release OFF CTest 1/1,
Release test-only ON CTest 2/2, focused ObserverTrace 1/1, OFF build exclusion,
and the producer/drain TSan probe passing. This fresh failure neither compiles
observer sources in OFF mode nor identifies an observer regression.

The next gate remains a compatible, clean full Debug ASan/UBSan/LSan result or
a reviewed resolution of the independently reproduced pre-existing Lua/sol2
LSan baseline (55,288 bytes in 611 allocations). No image build/push or
Server4 work is authorized.
