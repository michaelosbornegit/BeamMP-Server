# Observer trace-capture local gate revalidation — 2026-08-10T19:59:42Z

## Scope

Local source/build/test verification only at `feat/observer-trace-capture` revision
`f3e5159338b7d7ef5c652decb93c55ecbfc8cf95` before this evidence commit. No
server, container/image, deployment, runtime configuration, real trace capture,
public-server action, or sensitive configuration/data was accessed or changed.

No production behavior changed: relative to the preceding gate evidence commit,
the branch has documentation-only changes. The pinned upstream base remains
`176de6b5a5e1858ddeca9705a6ca5f8a717c0ae7`; `git diff --check` and the
restricted base comparison for `src/Client.cpp` and `src/TNetwork.cpp` passed.

## Passed gates

- Retained compatible Release observer **OFF** rebuilt and full CTest passed
  **1/1** in **0.64 s**. Its compile database and both OFF binaries had **0**
  observer option/source or `ObserverTrace`/`beammp::observer` symbol matches.
- Retained compatible Release test-only observer **ON** rebuilt and full CTest
  passed **2/2** in **0.70 s**; focused `ObserverTrace` passed **1/1** in
  **0.03 s**. ON had **63** observer compile-database matches and **37/85**
  observer server/test symbols.
- Debug observer-ON ASan/UBSan rebuilt and, with leak detection disabled,
  passed full CTest **2/2** in **1.16 s** under halt-on-error options.
- A freshly compiled host GCC 15.2 ThreadSanitizer producer/drain probe (four
  producers × 2,000 records) exited **0** under
  `TSAN_OPTIONS=halt_on_error=1:exitcode=66`.

## Blocking result

The hard remaining local gate is the previously reproduced unsuppressed full
Debug LeakSanitizer baseline: after all unit assertions and `ObserverTrace`
complete, LeakSanitizer reports **55,288 bytes in 611 allocations** rooted in
existing `TLuaEngine` and vendored Lua/sol2 lifecycle paths. No suppression,
dependency workaround, or observer behavior change was introduced.

Do not build/push an image, inspect/configure Server4, deploy, enable runtime
capture, or capture real traces until that baseline receives a reviewed
resolution and the separate operational scope is documented.
