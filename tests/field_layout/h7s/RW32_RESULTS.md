# RW32: separate read and write cache lines

Author: Ruslan Kovtun (shpegun60), codexAi. License: MIT.

RW32 is the production Field layout. On Cortex-M7, alignment remains 32 bytes
and each array element remains **96 bytes**, the same as B32. The measured
benefit is largest for writes to the 1024-field RAM table. It is not a claim
that every operation on every target becomes faster.

## Layout and compatibility

The CubeIDE ARM build verifies these offsets in every Field:

| Member/data | Offset | Size |
|---|---:|---:|
| Getter | 0 | 12 |
| Immutable read type | 12 | 1 |
| ID, name pointer, unit pointer | 16, 20, 24 | 4 each |
| Setter | 32 | 8 |
| FieldType numeric tag and restricted flag | 40, 41 | 1 each |
| Typed minimum/maximum storage | 48 | 16 |
| Default Scalar | 64 | 16 |
| Enum description pointer | 80 | 4 |

Getter and read type are in the first line. Setter, write type, restricted
flag and both limits fit in the second line. Defaults and enum metadata are
outside these access paths. Names and ID fill unused space in the first line.
This describes Field's own data accesses, not the catalog, delegate target,
owner data, instructions or call stack.

FieldType is now 48 bytes. It stores a private typed bounds union whose active
member is selected together with its numeric tag; callers cannot change either
independently. Whole-object copying preserves both. The public Scalar still
uses `std::variant`; the numeric conversion policy and serialized schema are
unchanged. A duplicated read tag avoids accessing the second line on reads.

The constexpr constructor preserves positional table rows and defaults.
Public reads such as `field.declaredType.minimum()` remain unchanged. Field
members are now const: direct metadata edits and whole-Field assignment are
rejected; copy/move construction remains trivial. Bound owners remain mutable.
Construct a replacement table when definitions change, keeping borrowed
storage alive. See the [migration contract](../../../lib/telemetry/README.md#field-abi-migration-and-storage).

ABI revision is **3**, even though ARM Field size did not change. Rebuild all
translation units and static libraries. `TelemetryCacheline.h` chooses a
compile-time alignment policy: Cortex-M uses 32, ordinary desktop targets 64,
Apple ARM 128. `TELEMETRY_FORCE_CACHELINE` overrides it and must be identical
throughout a program. It is not runtime hardware discovery. On Qt/MinGW's
64-bit ABI the default produces a 128-byte Field aligned to 64; the STM32
layout remains 96 bytes aligned to 32.

## Measured comparison

NUCLEO-H7S3L8, M7 at 600 MHz, enabled I/D caches, 32 KiB D-cache;
CubeIDE GCC 14.3.1, C++17, hard float, no LTO, exceptions or RTTI.
B32 uses the pinned historical source; Current uses RW32 from this change.
The [harness contract](README.md) describes memory placement and coverage.

Four images passed **1840 windows / 60,293,120 timed calls**. Each number is
the median of five 32768-call windows after 2048 warmup calls. Interrupts were
masked during each window. Loop, call and result-consumption costs are included.
All setup checks, coverage and independently computed result checksums passed.

Large RAM table, 1024 fields, deterministic shuffled IDs; cycles per call:

| Operation | B32 O2 | RW32 O2 | B32 Os | RW32 Os |
|---|---:|---:|---:|---:|
| Lookup | 26.001 | 26.001 | 23.002 | 23.002 |
| Scalar read | 83.870 | 82.880 | 86.995 | 86.014 |
| Float read | 78.983 | 78.247 | 76.486 | 75.484 |
| Float write | 90.007 | 80.896 | 98.044 | 79.888 |
| U16 write | 77.383 | 66.521 | 87.958 | 70.198 |

Here reading improves about 1%; writes improve 10.1%/14.0% at O2 and
18.5%/20.2% at Os. Small tables and known/repeated IDs have different costs.
For example, Flash with 128 shuffled fields gives unchanged O2 float reads
(77.914 cycles) and nearly unchanged float writes (80.793 to 80.764).
Some repeated-ID float-write controls cost one extra cycle at O2 (81 to 82,
90 to 91). The [complete samples](rw32-samples.csv) retain those cases too.

An earlier candidate had a 3–4% Os read regression: GCC outlined the small
finite-value test, adding an FPU register save/restore to the read wrapper.
`TELEMETRY_FORCE_INLINE` on that test removed the extra `vpush/vpop` pair.
The table above is the full repeated run after this fix. ARM checks reject
an outlined finite-value helper in the codegen probes.

## Storage and generated code

The 1024-row table is 98,304 bytes in both layouts. Read-only Probe object
section totals include the table, strings and other constants:

| Optimization/object | B32 .text | RW32 .text | B32 .rodata | RW32 .rodata |
|---|---:|---:|---:|---:|
| O2 Probe | 12,128 | 12,056 | 98,569 | 98,569 |
| Os Probe | 11,910 | 11,534 | 98,550 | 98,550 |
| O2 JSON | 10,352 | 10,452 | 552 | 408 |
| Os JSON | 6,958 | 6,744 | 524 | 380 |

These objects have zero `.data`/`.bss` and no startup constructors. Full linked
benchmark images, which also include setup and the harness, change from
44,800 to 44,992 bytes at O2 (+192) and 44,576 to 44,416 at Os (-160).
Equal table size therefore does not mean every application binary has equal
Flash usage. Link placement and the rest of the application also matter.

## Verification and retained evidence

- GCC C++17/C++20: 585/587 runtime checks, 35 existing compile rejections and
  nine new immutable-Field rejections; standalone headers and floating flags.
- Clang 18 C++17: the same checks with ASan, UBSan, float-cast-overflow and
  stack scope/return checking. Constexpr enum construction passes on Clang.
- Descriptor copy/move/assignment coverage includes 676 cross-type transitions,
  native and custom bounds, null/unknown tags, wide integers and enums.
- The B32/RW32 fixture emits byte-identical schema and value JSON, including
  the schema fingerprint; constructor/copy/read/write contracts pass for both.
- Nine cache-line configurations, five invalid override cases and explicit
  64/128-byte Field layouts; ARM asserts pin actual STM32 offsets and size.
- CubeIDE O2/Os: 17 translation units, seven read-only codegen probes and a
  newlib-nano consumer link. Qt Release build and offscreen startup pass.
- The repeated [JSON stack run](../../json_stack/README.md) passes 588 windows;
  largest observed schema stack writes remain 968 bytes at O2, now 936 at Os.

[rw32-receipt.json](rw32-receipt.json) records image/object/source hashes,
compiler inputs, checked coverage and original-image restoration. The retained
local artifact directory is `build/rw32-live-inline`. Verify offline with:

```sh
python tests/field_layout/h7s/verify.py --receipt tests/field_layout/h7s/rw32-receipt.json --samples tests/field_layout/h7s/rw32-samples.csv --self-test
```

Adding `--artifacts build/rw32-live-inline` checks the actual retained object,
ELF and binary bytes. Historical B32 receipts are kept separately and do not
claim RW32 object equivalence. Hashes identify evidence; they do not authenticate
physical measurements by themselves.

A fresh build of the publication sources reproduced both measured RW32 ELF
and binary images byte for byte, including both layout and JSON-stack fixtures
at O2/Os. The layout Probe/Benchmark objects also matched. These checks are
retained in `build/rw32-publish-layout` and `build/rw32-publish-json`; later
documentation/comment edits did not change the measured implementation.

The original 64 KiB board firmware was restored and read back after the run:
`a5903024dba85fab5121150ca8ad13482f97384aa450aab67413881991fb9456`.
These measurements establish the listed H7S fixture behavior. H753 firmware
timing, RTOS task stack peaks, arbitrary callbacks, interrupt latency and cold
single-call behavior are not established by this experiment.
