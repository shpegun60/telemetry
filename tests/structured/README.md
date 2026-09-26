# Structured telemetry implementation checkpoints

Authors: Ruslan Kovtun (shpegun60), codexAi. MIT license.

The implementation contract is
[StructuredTelemetryV3ImplementationPlan.md](../../doc/StructuredTelemetryV3ImplementationPlan.md).
This directory records evidence as each stage is implemented. The structured
library is not present at the Stage 00 checkpoint.

## Stage 00: scalar baseline

Tracked scalar source: `5a289a8f12a85f68219275abcde7b58d925076ac`,
"Close telemetry follow-up contract and debug gaps". Local `HEAD`,
`origin/main`, and `git ls-remote origin refs/heads/main` all identified this
commit before the first structured change. The source files tracked by Git
were unchanged. The two implementation plans and `tests/review-2026-09-26/`
were separate untracked material; the review tree was not included in the
baseline or modified here.

[Exact-SHA CI run 36253273669](https://github.com/shpegun60/telemetry/actions/runs/36253273669)
completed successfully: all nine jobs, including GCC/Clang C++17 and C++20,
the Clang sanitized job, Cortex-M7 compile/link, and Qt. This prior run is
the software gate for the existing scalar code; no new local full-suite result
is claimed for Stage 00.

The current scalar ABI is revision 8 in
[`TelemetryAbi.h`](../../lib/telemetry/abi/TelemetryAbi.h).
The existing ARM instruction and storage gates are
[`run_arm_checks.py`](../run_arm_checks.py),
[`FieldTableCodegen.cpp`](../FieldTableCodegen.cpp),
[`CommandTableCodegen.cpp`](../CommandTableCodegen.cpp), and
[`IndexCodegen.cpp`](../IndexCodegen.cpp). Their source remains the frozen
comparison point for the new module. The resource checks and binary v2.1
adapter are part of the same CI run.

The CI ARM compiler was `arm-none-eabi-g++` 13.2.1. Its retained artifact
`cortex-m7-check-logs` contains 44 disassemblies and 18 stack-usage files.
The committed [baseline data](baseline-arm-gcc13.json) preserves normalized
instruction streams for ten representative Field/Command/Index symbols at
`-O2` and `-Os`, along with linked resource section sizes and maximum
per-function stack frames. The complete CI logs can be retrieved with:

```powershell
gh run download 36253273669 --name cortex-m7-check-logs --dir build/structured-baseline/ci-arm-logs
python tests/structured/capture_scalar_baseline.py --logs build/structured-baseline/ci-arm-logs --source-sha 5a289a8f12a85f68219275abcde7b58d925076ac --ci-run https://github.com/shpegun60/telemetry/actions/runs/36253273669 --output tests/structured/baseline-arm-gcc13.json
```

| Optimization | Linked resource `.text` | `.rodata` | `.data` | `.bss` | Maximum Schema/Commands frame |
| --- | ---: | ---: | ---: | ---: | ---: |
| `-O2` | 29,372 B | 2,336 B | 116 B | 600 B | 176 B |
| `-Os` | 20,156 B | 2,272 B | 116 B | 600 B | 160 B |

These are sections of the CI **resource fixture**, not the H753 firmware
image. Stack figures are individual frames, not the sum of a call chain.
For a future local comparison, use the same compiler and fixture; the
installed CubeIDE compiler is a different version (14.3.1).

The archived [H7S review receipt](../regression/h7s/current-receipt.json)
was checked with `verify.py --current-code --self-test`. All 77 recorded
library input hashes matched this source; the receipt also passed its 11
mutation controls. Its O2/Os run and restoration remain **archived** evidence,
not a new board measurement at this checkpoint.

Available tools inspected at Stage 00: Python 3.13.14, Qt MinGW GCC 13.1.0,
WSL Clang 18.1.3, and CubeIDE ARM GCC 14.3.1. New structured probes should
record the compiler and flags they actually use.

## Stage 01: C++20 reflection backend

The unmodified Boost.PFR 1.92.0 header tree and its Boost Software License
are in [`lib/boost_pfr`](../../lib/boost_pfr/VERSION.md). It is pinned to
upstream commit `401385c240027423acbb1eb6dea2abe0043db5aa`. Only PFR was
copied: 44 headers and the license. Existing
`magic_enum` remains pinned at v0.9.8. The PFR source bytes are not a
measurement of linked firmware size.

[`PfrProbe.cpp`](reflection/PfrProbe.cpp) uses PFR's C++20 names and
structured-binding engine for two scalar fields, repeated types, a nested
aggregate, `std::array`, mutable/const reference access and names that are
identical in a second translation unit. It exercises ordinary and signed
enums, a sparse value outside `magic_enum`'s default scan, and numeric aliases.
The automatic sparse scan finds only `None`; an explicit
`enum_name<SparseMode::Far>()` does find `Far`. Aliases share a numeric code;
the selected name is compiler-dependent. The probe rejects a non-ASCII
automatic member name and accepts an explicit UTF-8 string as data. The
actual public-name validation belongs to the facade stage.

Run the focused checks with the vendored headers:

```text
python tests/structured/reflection/run.py --cxx g++ --build-dir build/structured-pfr-gcc
python tests/structured/reflection/run.py --cxx clang++-18 --build-dir build/structured-pfr-clang
python tests/structured/reflection/run.py --arm --cxx arm-none-eabi-g++ --build-dir build/structured-pfr-arm
```

The runner records each compiler command, diagnostic and elapsed time in the
chosen build directory. The negative source must fail for its specific ASCII
diagnostic. CI runs the host C++20 probe on GCC and Clang and the ARM probe
at `-O2`, `-Os` and `-Og`.

| Local compiler | Probe result | Typical positive compile time per unit | ARM object sections, O2/Os |
| --- | --- | ---: | --- |
| Qt MinGW GCC 13.1.0 | host link/run and negative check pass | 0.5–0.8 s | — |
| Ubuntu GCC 13.3.0 | host link/run and negative check pass | 0.7–1.1 s | — |
| Ubuntu Clang 18.1.3 | host link/run and negative check pass | 0.7–1.2 s | — |
| ARM GCC 13.2.1 | O2/Os/Og compile and negative check pass | 0.6–1.0 s | Probe 150 B `.text`, Other 4 B `.text`; zero `.data/.bss` |
| CubeIDE ARM GCC 14.3.1 | O2/Os/Og compile and negative check pass | 0.6–1.0 s | Probe 146 B `.text`, Other 4 B `.text`; zero `.data/.bss` |

The ARM size rows describe the **test objects**, which include `main` and
`printf`; they do not isolate PFR overhead or predict the final firmware.
No STM32 board run is claimed for this header-only compiler probe. Stage 02
can now build the stable reflection facade on this verified backend.

## Stage 02: stable reflection facade

The new [`reflection`](../../lib/telemetry_structured/reflection) namespace
contains the sole aggregate and callable traits interface for future
Registry/Codec/Model code. Only `reflection/detail/PfrAdapter.hpp` includes
Boost.PFR; direct vendor symbols are forbidden outside the backend adapter.
`Aggregate.hpp` exposes `memberCount`, exact `MemberType`, `memberName` and
`get`. The facade validates compiler-derived member names as ASCII
identifiers. A mutable or const lvalue keeps its member reference and cv.
PFR copies a member when passed a temporary aggregate; the facade rejects
that call so member access remains reference based and copy free.

`Callable.hpp` provides `Function<Signature>` facts for free functions,
function pointers, member functions with all cv/ref/noexcept combinations,
unique callable objects and the four existing slot families. Its
`EndpointTraits` validates the common shape before normalizing one request
and unwrapping a `ServiceResult<Response>` return. It preserves the original
Result and Arguments for factory-specific checks. Only Service has a
`Response` payload; CommandResult and WriteResult remain operation statuses.
`ServiceResult` storage is deferred to Stage 06; these traits need only its
declared type. `Enum.hpp`
defines the explicit specialization point and planned normalized interface;
the actual dictionary is the next stage.

[`facade/run.py`](facade/run.py) checks a host executable and a second
translation unit, 14 distinct compile-time rejection reasons, and the
source-level vendor boundary. ARM runs compile at `-O2`, `-Os` and `-Og`.
It passed locally on Qt MinGW GCC 13.1, Ubuntu Clang 18.1, ARM GCC 13.2 and
CubeIDE ARM GCC 14.3. The CubeIDE O2/Os test objects have 20/18 total bytes
of `.text`, respectively, and no `.data/.bss`; these are small facade
fixtures, not a firmware-size comparison. The new CI steps run the same
checks on host GCC/Clang and Cortex-M7. They do not claim a board run.
