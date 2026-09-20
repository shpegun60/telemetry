# Compact callback exact A/B result

Author: Ruslan Kovtun (shpegun60), codexAi. License: MIT.

This is the final exact comparison of production checkpoint `a452283` with
the compact two-word Getter/Setter implementation. Both candidates retain the
RW32 Field layout: 32-byte alignment and a 96-byte ARM32 stride. Getter shrinks
from 12 to 8 bytes; `readType` moves from offset 12 to 8. Setter remains 8 bytes
at offset 32 and `declaredType` remains at offset 40.

The compact callbacks store one exact payload and one generated invoker. Native
function pointers keep their real type; object callbacks keep a borrowed object
pointer. No call uses a converted function-pointer type, allocation or owned
closure. Under GCC/Clang `-Os`, the two-load Setter dispatch remains out of line
to avoid extending an invoker register across Field conversion and bounds code.

## Measured comparison

NUCLEO-H7S3L8, Cortex-M7 at 600 MHz, enabled I/D caches and 32 KiB D-cache;
CubeIDE GCC 14.3.1, C++17, hard float, no LTO, exceptions or RTTI. Four images
completed **1840 windows / 60,293,120 timed calls**. Each result below is the
median of five 32768-call windows after 2048 warmup calls. Interrupts were
masked during each window. The loop, dispatch and result-consumption cost is
included.

Large cacheable RAM table, 1024 fields, deterministic shuffled IDs; cycles per
call:

| Operation | `a452283` O2 | Compact O2 | Change | `a452283` Os | Compact Os | Change |
|---|---:|---:|---:|---:|---:|---:|
| Lookup | 26.001 | 24.001 | -7.69% | 23.002 | 22.002 | -4.35% |
| Scalar read | 82.874 | 78.675 | -5.07% | 86.014 | 83.045 | -3.45% |
| Float read | 78.238 | 71.814 | -8.21% | 75.484 | 74.077 | -1.86% |
| Float write | 80.890 | 80.596 | -0.36% | 79.888 | 79.888 | unchanged |
| U16 write | 66.519 | 57.640 | -13.35% | 70.198 | 70.740 | +0.77% |

The full linked image changes from 44,960 to 43,264 bytes at `-O2`
(-1,696 bytes) and from 44,416 to 43,712 bytes at `-Os` (-704 bytes). Field
arrays do not shrink because their 96-byte cache-line-aligned stride is
unchanged; the image saving comes from callback and dispatch code.

The remaining `-Os` U16 difference is not an additional conversion or callback
operation. Normalized disassembly of the complete runtime write wrappers is
identical between candidates: 323 instructions for F32 and 238 for U16. The
Setter dispatcher itself is identical:

```text
ldr r3, [r0, #4]
cbz ...
ldr r0, [r0]
bx  r3
movs r0, #2
bx  lr
```

Even the unchanged 16-instruction lookup varied by one cycle after link
placement changed. The +0.77% U16 result is a real, repeatable timing shift for
these concrete linked images. Because the complete write wrappers are
instruction-identical and the per-profile shift ranges from -1 to +1 cycle, the
evidence attributes it to function placement/alignment and instruction-cache
sensitivity, without evidence of additional algorithmic work. At `-O2`, where
this project optimizes production hot code, all five random-RAM operations
improve.

## Evidence and limits

[compact-callback-receipt.json](compact-callback-receipt.json) records the
compiler, exact baseline commit, normalized source hashes, ELF/BIN/object hashes,
device geometry and original-image restoration. [compact-callback-samples.csv](compact-callback-samples.csv)
contains every timing window. Verify both offline with retained binaries when
available:

```sh
python tests/field_layout/h7s/verify.py \
  --receipt tests/field_layout/h7s/compact-callback-receipt.json \
  --samples tests/field_layout/h7s/compact-callback-samples.csv \
  --self-test
```

Add `--artifacts build/compact-getter-exact-h7-dispatch-live` to authenticate
the retained Probe/Benchmark objects and ELF/BIN files too. The verifier checks
complete coverage, unique keys, independent result sums, image identity and
restoration, and rejects deliberately damaged evidence.

The original 64 KiB board image was restored and read back exactly:
`a5903024dba85fab5121150ca8ad13482f97384aa450aab67413881991fb9456`.

A fresh build from the publication candidate reproduced the measured Current
`Probe.o`, `Benchmark.o`, ELF and BIN byte for byte at both `-O2` and `-Os`.
Thus the later compile-time ABI accessors and documentation/demo work did not
change the measured callback/layout image.

This establishes steady-state behavior of the H7S fixture. It does not establish
H753 firmware timing, RTOS scheduling, cold single-call latency or arbitrary
user callback cost. The final source also has host/ARM compile, sanitizer,
layout and code-generation checks; compile-time ABI-guard additions after this
run do not alter the callback representation or measured read/write algorithm.
