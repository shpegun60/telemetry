# Resource checks

Authors: Ruslan Kovtun (shpegun60), codexAi. MIT.

From the repository root, with Python 3 and a C++20 compiler:

```sh
python tests/resources/run.py --build-dir build/resources
python tests/resources/run.py --cxx clang++-18 --sanitize --build-dir build/resources-san
python tests/resources/run.py --arm --cxx arm-none-eabi-g++ --build-dir build/resources-arm
```

`--objdump` and `--size` select matching ARM tools. For local STM32 work use
the CubeIDE toolchain. Build artifacts/logs stay in the requested directory.
These checks have no Qt or live-board dependency.

New C++ sources use explicit blocks, four-space indentation and a 100-column
limit. The shared style is [`.clang-format`](.clang-format); apply it explicitly
to the resource libraries, application facade and their checks:

```sh
clang-format --style=file:tests/resources/.clang-format -i path/to/source.cpp
```

- `CoreCheck`: constant/empty tables, borrowed const/mutable providers,
  READ-only/WRITE-only/RW capabilities, invalid indices and cursors, bounded
  ChunkWriter, byte-order golden packets, whole-entry LIST paging, repeated
  WRITE delivery, malformed lengths and guarded small response buffers.
- `Negative`: provider lifetime, owning temporary path strings, temporary
  table views, duplicate/invalid paths, missing callbacks, throwing size and
  size narrowing. A valid control compiles before rejection cases run.
- `NumberTextCheck`: two independent decimal-formatting oracles (`to_chars`
  and `snprintf`), every finite binary64 exponent, subnormals, rounding ties,
  decimal boundaries, random double/float samples and guarded output buffers;
  exact signed/unsigned integer extremes, including the signed minima.
- `StreamCheck`: every byte value and escaped-string continuation window,
  long labels, zero/small buffers, atomic getter preflight and u32 count
  overflow without allocating a large buffer.
- `TelemetryFilesCheck`: complete concatenation versus the existing field and
  grouped command JSON, with capacities 1/2/3/7/31/63/127/220/256/1024;
  deterministic metadata retries; empty/reserved records; long escaped labels;
  optional decimal-comma locale; all Scalar alternatives and unavailable
  tokens; exact size and getter counts; changing live readings; invalid cursors.
- `DeviceCheck`: the application facade, all three demo files and packet LIST,
  compiled without telemetry headers in the consuming translation unit.
- `NoHeapCheck`: reject C++ new/new[] during provider construction and transfers,
  including floating metadata and command schemas. C library implementation
  internals are not intercepted by this check.
- `AbiCheck`: each adapter links with a matching telemetry layout and fails
  to link when the caller changes the cache-line layout. Each failure has a
  matching successful control build.
- `ArmProbe`: Cortex-M7 `-O2`/`-Os` compilation/linking, FileEntry=16 bytes,
  offsets 0/8/12, view=8 bytes, constant descriptor storage and direct known-
  provider dispatch. Assembly/section logs are retained. Cycle performance
  requires separate hardware measurements; compilation is not a cycle test.
- `NumberTextProbe`: 32-bit integers retain native-width division, Stream
  occupies 48 bytes on ARM32, and stack-usage reports are retained. The linked
  fixture is also checked for accidental reintroduction of large Ryu tables.

The driver also compiles every public new header independently. Sanitized runs
use address, undefined-behavior and float-cast-overflow instrumentation.
Set `TELEMETRY_TEST_LOCALE` to a locally available decimal-comma locale to enable
the extra schema comparison. CI supplies `de_DE.UTF-8`.

## Local validation, 2026-09-21

Validated on the implementation based on telemetry `a81d19a`:

- Qt MinGW GCC 13.1, C++20: all six resource suites, standalone headers,
  compile-time rejection controls and three adapter ABI link controls.
  The decimal-comma run uses `German_Germany.1252`.
- Clang 18, C++20: the resource suites with address/undefined-behavior and
  float-cast-overflow sanitizers, including the no-C++-heap control.
- Qt 6.10.1: the playground builds in C++20 and its offscreen smoke test exits 0.
- Cortex-M7 GCC 14.3.1 from CubeIDE and GCC 13.2.1: `-O2` and `-Os`
  compile/link/layout/static-storage and dispatch checks pass.
- Existing telemetry regression: all 14 C++20 host suites and rejection/ABI
  checks pass. Its original ARM runner passes both optimizations (38 sources,
  16 constant probes, native instruction comparisons and mixed-ABI rejection).

No source under `lib/telemetry` is changed. These are local build/test results;
they are not a published CI result or hardware cycle measurement.

### MCU footprint and arithmetic

The first implementation used `std::to_chars` for floating bounds/defaults.
Its two largest ARM lookup tables alone occupied 104568 bytes (about 102 KiB).
The current adapter replaces it with an exact, bounded decimal formatter.
The numeric JSON contract is unchanged: 17 significant digits, nearest
ties-to-even rounding and general-format exponent selection.

For the complete linked demo fixture with GCC 14.3.1 and section garbage
collection, before and after these resource optimizations:

| Formatter / optimization | `.text` | `.rodata` | `.data` | `.bss` |
|---|---:|---:|---:|---:|
| Original `to_chars`, `-O2` | 47376 | 109120 | 128 | 420 |
| Current, `-O2` | 38236 | 2880 | 116 | 404 |
| Original `to_chars`, `-Os` | 36624 | 108976 | 128 | 420 |
| Current, `-Os` | 28132 | 2720 | 116 | 404 |

These are bytes for the complete fixture, including demo tables, adapters and
linked standard-library support; they are not the incremental size of the
header-only core. `size-*.log` and `dump-*.log` from the ARM runner reproduce
the measurements. The old telemetry serializers are still available separately.

The formatter needs 384 bytes of bounded decimal workspace. Its measured
individual stack frame is 464 bytes at `-O2` and 456 at `-Os`; these are not
whole-call-chain stack budgets. Very small/large exponents require more integer
arithmetic than ordinary metadata values. Removing ROM tables is a footprint
tradeoff, not a claim that this formatter beats `to_chars` in CPU time.

The subsequent arithmetic/stream pass uses 32-bit multiplication/division for
decimal limbs and ordinary integers, reduces Stream from 64 to 48 bytes, bounds
escape scanning by the current chunk, and writes live hex tokens directly into
their reserved span. It adds 280 bytes of `.text + .rodata` at `-O2` versus the
first compact formatter and saves 240 bytes at `-Os`. Core read/stat dispatch
instruction streams are unchanged at both optimization levels.

### Host timing experiment

[`Benchmark.cpp`](Benchmark.cpp) times numeric formatting and long-label
windows independently of transport and getters. Build/run it with:

```sh
g++ -std=c++20 -O2 -Ilib tests/resources/Benchmark.cpp -o build/resource-benchmark
build/resource-benchmark
```

For the arithmetic/stream pass, Qt MinGW GCC 13.1 `-O2`, best of five runs:

| Operation | Before, ns/call | After, ns/call |
|---|---:|---:|
| Common floating metadata | 55.96 | 31.25 |
| Varied finite binary64 values | 4113.20 | 2256.09 |
| Start of 8192-byte label, 64-byte output | 4780.47 | 281.37 |
| Resume label at byte 4096, 64-byte output | 4784.94 | 2527.28 |

Both builds produced the same checksums. These are host timings, not Cortex-M7
cycle measurements or end-to-end throughput. Metadata continuation still
regenerates the prefix of its current record and scans earlier catalog counts;
large enum/command records and very small chunks can therefore repeat work.
No board timing has been performed for this resource implementation.

Library modules: [resource core](../../lib/resource/README.md),
[protocol](../../lib/resource/protocol/README.md),
[telemetry adapters](../../lib/resource/telemetry/README.md).
