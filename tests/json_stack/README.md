# JSON stack measurements on NUCLEO-H7S3L8

Author: Ruslan Kovtun (shpegun60), codexAi. License: MIT.

CubeIDE GCC 14.3.1, C++17, Cortex-M7 hard float, newlib-nano with
`-Wl,-u,_printf_float`, no LTO, exceptions or RTTI. The connected H7S3 ran at
600 MHz. This measures the library and formatter stack on that build, not
FreeRTOS task stack usage in the H753 firmware.

## Observed high-water marks

Maximum changed-stack bytes over the recorded cases, including nested C
library calls. The output buffer and catalog tables were static, outside
the measured stack.

The B32 columns preserve the `9f95e49` checkpoint; RW32 records the initial
RW32 publication. The final columns repeat the complete run after the ABI link
guard and null-metadata hardening.

| Operation | B32 O2 | B32 Os | RW32 O2 | RW32 Os | Guard O2 | Guard Os |
|---|---:|---:|---:|---:|---:|---:|
| 512-byte instrument control | 512 | 512 | 512 | 512 | 512 | 512 |
| Native-bound schema | 848 | 824 | 848 | 816 | 840 | 816 |
| Custom float/double bounds and enum schema | **968** | **944** | **968** | **936** | **960** | **936** |
| F32 values, 21 input cases | 792 | 768 | 792 | 768 | 792 | 768 |
| F64 values, 21 input cases | **880** | **856** | **880** | **856** | **880** | **856** |
| U64/S64/U32/S32 extremes and bool | 576 | 512 | 576 | 512 | 576 | 512 |
| Truncated schema | 544 | 584 | 544 | 576 | 536 | 576 |
| Truncated floating values | 752 | 728 | 752 | 728 | 752 | 728 |
| Null output buffer | 56 | 116 | 56 | 108 | 56 | 108 |

Each complete run has two images with 294 windows each, **588 total**.
Each case used three repeats
and two complementary fill patterns. The float cases include signed zero,
subnormals, smallest normals, finite extremes, fractional/exponent values,
infinities and NaN. The host checked complete unique coverage, JSON contents,
exact 64-bit integers, float/double round trips, schema limits/dictionary and
output checksums. The original 64 KiB firmware was restored and read back:
`a5903024dba85fab5121150ca8ad13482f97384aa450aab67413881991fb9456`.

## How the measurement works

An assembly trampoline enters an independent 16 KiB, 8-byte-aligned PSP stack
in DTCM, while keeping the caller's saved registers on the original MSP.
It runs only from privileged Thread mode, with maskable interrupts disabled.
It restores CONTROL and PSP before returning; the harness checks both.
The bottom 256-byte guard must remain untouched. There is no C++ stack-pointer
switch inside a compiler-generated function prologue. See [StackCall.S](StackCall.S).

Every window fills the stack before the call and scans afterwards. UART,
setup, result checking and the caller's own stack are outside this region.
The volatile control must touch at least 512 bytes; it measured exactly 512
in both builds. The recorded compiler frames are 512 bytes for the control
and zero for the dispatch wrapper.
The two fill patterns reduce accidental matches with written data. This is
an observed memory-write watermark, **not a mathematical bound on the deepest
stack pointer**: reserved but untouched stack slots need not change a canary.
Neither NMI/fault nesting nor arbitrary application getters are covered.

The historical B32 `.su` data illustrates why individual frames are insufficient:

| Standalone compiler frame | O2 | Os |
|---|---:|---:|
| `writeSchema(CatalogIndex, ...)` | 144 | 128 |
| `writeValues(CatalogIndex, ...)` | 104 | 88 |
| Floating append helper | 104 | 96 |
| Pointer/count overload wrapper | 24 | 24 |

These are per-function frames, not the sum along a call chain. The runtime
watermark includes newlib routines for which this project has no `.su` file.
Formatting may also use newlib heap storage; this fixture provides a working
heap and does not measure heap peaks.

For integration, add local JSON buffers, caller/getter stack, wrapper costs
and the RTOS/interrupt context to the budget. A 1 KiB task stack is already
too close to this observed serializer-only peak to justify its use. Recheck
the actual task high-water mark with its toolchain, memory map, options,
callbacks and traffic; these values do not establish a universal safe stack
size or H753 execution timing.

## Reproduce

Use the copied Cube scaffold described in the
[H7S harness instructions](../field_layout/h7s/README.md). Neither runner edits
the original COBS project. All generated output belongs in a fresh directory.

```powershell
$armCompiler = (Get-ChildItem 'C:/ST/*/STM32CubeIDE/plugins/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32*/tools/bin/arm-none-eabi-g++.exe' | Select-Object -First 1).FullName
# Compile/link only; no board access:
python tests/json_stack/run.py --cube build/field_layout_experiment/h7s/scaffold --arm-cxx $armCompiler --output build/json-stack-build
# Build, back up, program, measure, then restore and read-back-verify:
python tests/json_stack/run.py --cube build/field_layout_experiment/h7s/scaffold --arm-cxx $armCompiler --output build/json-stack-live --run --serial 002A001F3033510135393935 --port COM6
# Offline check, including deliberately damaged evidence:
python tests/json_stack/verify.py --self-test
# Current RW32 evidence:
python tests/json_stack/verify.py --receipt tests/json_stack/rw32-receipt.json --self-test
# Current ABI-guard/null-metadata evidence:
python tests/json_stack/verify.py --receipt tests/json_stack/abi-guard-receipt.json --self-test
# Also compare retained local ELF, binary and object bytes:
python tests/json_stack/verify.py --receipt tests/json_stack/rw32-receipt.json --self-test --artifacts build/rw32-json-live-2
python tests/json_stack/verify.py --receipt tests/json_stack/abi-guard-receipt.json --self-test --artifacts build/json-stack-abi-release-live
```

The serial and port must identify the intended board. Both images are built
and constrained to the backed-up 64 KiB internal Flash before programming.
A `finally` block restores the original image; an interrupted host or power
loss still requires restoration from the retained `before.bin`.

[receipt.json](receipt.json) preserves the B32 baseline in
`build/json-stack-live-1`. [rw32-receipt.json](rw32-receipt.json) records the
initial RW32 run in `build/rw32-json-live-2`.
[abi-guard-receipt.json](abi-guard-receipt.json) records the current guarded
serializer run. Each contains every measurement,
JSON result, image/object identity, source hashes, compiler frames and
restoration hashes. Full ELF/binary/object files,
programmer/UART logs and original firmware stay there. Shared hashes identify
those artifacts but are not board attestation. Every run restored the same
original 64 KiB image with SHA-256
`a5903024dba85fab5121150ca8ad13482f97384aa450aab67413881991fb9456`.
CI checks the recorded evidence
and rejects ten mutation controls without requiring a board.

The preceding local attempt `build/rw32-json-live` stopped on an ST-LINK
programming failure before the Os measurement. Its finally block restored and
verified the original image. It is retained locally and excluded from these
results; the complete retry above passed both images and restoration.
