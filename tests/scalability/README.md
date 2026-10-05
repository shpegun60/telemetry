# Scalability checks

Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.

[Measured findings](../../doc/Scalability.md) · [Test index](../README.md) ·
[Module fixture](modular/README.md) · [Runtime integration](../../doc/user/Model.md) ·
[Recorded profiles](results.json)

Measure endpoint counts, repeated roots, exact type diversity, visitor types
and consumer dependencies separately. A passing erased index population is not
proof that a typed Model of the same size compiles. Formal acceptance ceilings
are not compiler or device memory budgets.

| Runner | Population and evidence | What it does not establish |
| --- | --- | --- |
| [compile_tables.py](compile_tables.py) | Fields/Commands/Services/Mixed; single vs sharded tables; table-only vs Model; GNU time and peak RSS; emitted sections; every row executed | Arbitrary distinct DTOs, all targets, MCU cycles |
| [compile_types.py](compile_types.py) | Shared vs distinct exact DTOs; registry vs Model/codec/descriptor; structural boundaries; compiler budgets | Guaranteed 4096-type capacity on default compilers |
| [runtime.py](runtime.py), [Runtime.cpp](Runtime.cpp) | Real erased handlers over caller-owned row arrays: 32/128/1024/16384/65536 rows **per separate family index**; exact success status, bytes and owner state | A typed 65536-row Model or a 196608-endpoint descriptor |
| [runtime.py](runtime.py), [Resources.cpp](Resources.cpp) | Actual typed Model; runtime/constexpr/packed descriptor; full/chunked byte equality; Values tokens/getter counts; cursor refusal; executable hashes | Device throughput, arbitrary large Model acceptance |
| [runtime.py](runtime.py), [Layout.cpp](Layout.cpp) | ARM ABI sizes, disassembly, sections and individual `.su` frames at O2/Os/Og | Execution or full call-chain stack on a board |
| [visitors.py](visitors.py) | External copy of the retained qualification Scale fixture; 0/1/2/4/8 visitor types; named reuse; host execution and ARM objects | Linked firmware size, hardware cycles or a new historical receipt |
| [modular.py](modular.py) | Independent module `.cpp` files, one registry, typed-header vs runtime consumers; dependency fan-out; bounded peers; no allocations in checked operations | Automatic merging of fully erased private schemas or an OS transport |

Generated sources, logs, objects and executables stay in an explicitly supplied
**external output directory**. Use a fresh directory for each series to retain
earlier evidence. Linux/WSL compile measurements require GNU `/usr/bin/time`,
`size` and C++20 GCC/Clang. Runtime, resource, visitor and modular fixtures also
support native Windows tools. ARM mode requires an installed Cortex-M7 C++
compiler and its sibling binutils; it never connects to hardware.

## Bounded correctness profiles

Run from the repository root. Select `--baseline` from the actual checkout for
the type-graph runner; its default is the recorded audit baseline, not a moving
branch name.

```sh
python3 tests/scalability/compile_tables.py --cxx g++ \
  --build-dir /tmp/telemetry-scale-tables \
  --rows 32 128 --families field command service mixed \
  --layouts sharded --stages model --require-pass

python3 tests/scalability/compile_types.py --baseline "$(git rev-parse HEAD)" \
  --build-dir /tmp/telemetry-scale-types --counts 32 128 \
  --compilers g++ clang++-18 --layers registry model --backend-boundaries --require-pass

python3 tests/scalability/runtime.py --cxx g++ \
  --build-dir /tmp/telemetry-scale-runtime
python3 tests/scalability/runtime.py --cxx clang++-18 --sanitize \
  --build-dir /tmp/telemetry-scale-runtime-sanitized
python3 tests/scalability/runtime.py --cxx clang++-18 --sanitize --resources \
  --build-dir /tmp/telemetry-scale-resources-sanitized

python3 tests/scalability/visitors.py --cxx g++ --rows 128 \
  --build-dir /tmp/telemetry-scale-visitors
python3 tests/scalability/modular.py --cxx g++ \
  --build-dir /tmp/telemetry-scale-modular
```

`--require-pass` makes unexpected compilation, link, execution or capacity
results fail validation. Exploratory table/type runs can instead retain compiler
limits as findings. Table runner distinguishes skipped larger profiles from
attempted rejections. `--generate-only`/`--prepare-only` is preparation, never a
claim that compilation or execution passed. Sanitizer timings are diagnostic
only and must not be compared with release measurements.

CI uses the bounded profiles above, plus ARM layout and 128-row visitor objects,
and uploads all generated outputs even on a failure. It gates correctness and
the ability to build these profiles. It deliberately does not gate elapsed time,
RSS or host nanoseconds across different machines. Larger local exploration is
retained separately; its rejected profiles do not turn into successful CI cases.

## Capacity exploration

Compile one series at a time when comparing durations. Defaults for table/type
series are 240 seconds per compile/link and a 4096 MiB **address-space** limit;
GNU maximum RSS is a separate recorded metric. Full models with hundreds of
unique DTOs can consume multiple GiB of compiler memory. Start small.

```sh
python3 tests/scalability/compile_tables.py --cxx g++ \
  --build-dir /tmp/telemetry-scale-shards --rows 512 1024 \
  --families field --layouts sharded --stages tables model

python3 tests/scalability/compile_types.py --baseline "$(git rev-parse HEAD)" \
  --build-dir /tmp/telemetry-scale-registry --counts 32 128 256 512 \
  --compilers g++ clang++-18 --layers registry

python3 tests/scalability/compile_types.py --baseline "$(git rev-parse HEAD)" \
  --build-dir /tmp/telemetry-scale-boundaries --counts 1 --layers registry \
  --compilers g++ --boundaries

python3 tests/scalability/runtime.py --arm --cxx arm-none-eabi-g++ \
  --build-dir /tmp/telemetry-scale-arm-layout
python3 tests/scalability/visitors.py --arm --cxx arm-none-eabi-g++ \
  --build-dir /tmp/telemetry-scale-arm-visitors
```

`--backend-boundaries` checks the pinned PFR200 positive control and requires
the exact PFR diagnostic for201. It is separate from the normalized-model
boundary suite. The latter retains an **unexpected rejection** of its 256-member
positive control: the pinned C++20 Boost.PFR generated backend supports only
200 direct aggregate members. `maxStructMembers == 256` is the normalized-model
ceiling, not backend capacity. Keep each DTO within the backend limit, using
nested DTOs when appropriate. Do not treat `all_expected: false` as green.

Explicit diagnostic overrides are recorded, never silently added:
`compile_tables.py --template-depth 2048` for repeated roots, and
`compile_types.py --compiler-extra-flag=-fbracket-depth=1024` for the measured
Clang unique-type case. These are not universal recommendations or guarantees.

## Read the evidence correctly

Each runner captures compiler identity, input hashes and commands. Compilation
profiles also retain failed diagnostic logs and generated source hashes. Runtime
and resource images have SHA-256; ARM sections describe objects. Visitor runs
recheck both original and generated source inputs, preserving partial summaries
when compilation fails. Local artifacts are not silently relabeled with a later
publication SHA.

The [report](../../doc/Scalability.md) records single-trial compiler measurements,
median host runtime/resource measurements and their scopes. Small timing
differences are not portable speed guarantees. Read the library input identity,
compiler flags, population and failure class before comparing numbers.
