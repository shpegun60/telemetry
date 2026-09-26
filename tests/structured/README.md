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

Pending the PFR/magic_enum probe. Its compiler and behavior results belong
here before starting the stable facade of Stage 02.
