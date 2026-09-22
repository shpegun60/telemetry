# H7S binary-resource measurements

Authors: Ruslan Kovtun (shpegun60), codexAi. MIT.

The harness compares the exact `f1cfbd891ef2f0c23f0fb2dae70be8ed4bd6fcaf`
baseline with a snapshot of the current library. It reuses the copied Cube
scaffold described in [the field benchmark](../../field_layout/h7s/README.md),
without modifying the original COBS project. Use the CubeIDE compiler.

This is the retained **2.0 direct-cursor experiment**, pinned to its original
wire bytes. To reproduce it, use the `c5739b2` checkout and the copied Cube
scaffold. The later 2.1 hardening deliberately changes version bytes and
fingerprints, so a current-tree run against f1cfbd8 cannot pass this fixture's
cross-version byte-equality assertions. Those assertions are not relaxed to
hide a format change. The retained receipt verifier remains runnable on current
source; it validates the recorded measurements, not new 2.1 MCU timings.

```powershell
python tests/resources/h7s/run.py --arm-cxx <path-to-arm-none-eabi-g++.exe> --output build/resource-measurement
python tests/resources/h7s/run.py --arm-cxx <path-to-arm-none-eabi-g++.exe> --output build/resource-live --run --serial 002A001F3033510135393935 --port COM6
```

The first command only builds. `--run` backs up the entire 64 KiB internal Flash,
verifies each programmed image, then restores the backup and compares a fresh
read-back hash. All images must fit that region. Option bytes and external memory
are not programmed. A power or host-process interruption requires restoration
from the retained `before.bin`; a failed restore is never reported as success.

The target boots into idle. The host must receive `RESOURCE IDLE 2` after `P`
before sending `R`. Resource construction happens only after that command, so a
reset never automatically repeats an interrupted measurement. Both images use
the same MPU setup, including the H7RS workaround described below.

The fixture contains 256 catalogs, two fields per catalog, and one command with
F32/enum parameters. It measures first/last field-schema and command-schema READ,
first/last fixed-width value READ, and the original sequential enum and parameter
visitors. Payload checksums must agree between revisions. Metadata must invoke
zero getters and each value request exactly one getter. DWT measures 256 calls
per window, five windows per operation, with interrupts masked and caches warmed.
The reported time includes dispatch, bounded encoding and checksum consumption.

Before timing, all three files are read completely with 31- and 256-byte output
buffers. Sizes, checksums and exact getter counts must agree across both chunk
sizes and all four images. Repeated EOF must remain empty. A two-byte value
buffer and a cursor into the middle of a token must invoke no getter. Current
also executes indexed enum/parameter traversal and rejects out-of-range indices.

The independent 16 KiB DTCM PSP buffer measures the complete nested call path,
not only the outer READ frame. Two fill patterns and a 512-byte positive control
check the watermark; a 256-byte guard catches a used-up test stack. UART output
and integer formatting occur outside the measured calls. The PSP trampoline is
shared with [the JSON stack test](../../json_stack/README.md).
The reported PSP maximum includes exercise, provider, metadata/getter and checksum
calls. It excludes the trampoline's 16 bytes on MSP, startup constructors and IRQ
frames. It is a measurement of this fixture, not a bound for arbitrary callbacks.

The output directory retains source/object/image hashes, UART records, compiler
logs, disassembly, stack-usage reports and `receipt.json`. `completed` and
`restored_and_verified` must both be true before a run counts as evidence. Failed
attempts are diagnostics, not cycle measurements. No board result is inferred
from host timing or instruction counts.

## Measured completion, 2026-09-22

[Retained receipt](direct-cursor-receipt.json): NUCLEO-H7S3L8, revision Y,
ST-LINK `002A001F3033510135393935`, core 600 MHz, GCC 14.3.1 from CubeIDE 2.0.0,
hard-float FPv5-D16, no LTO, I/D caches enabled. Library baseline is `f1cfbd8`;
current library is `260e168`. The worktree was dirty only with the measurement
harness and evidence changes. The receipt records exact library, fixture, object
and image hashes. The library itself was not modified for these measurements.

All four images completed: **160 timing windows, 72 stack probes and 24 complete
file transfers**. The complete schema/commands/values files have lengths
60230/71724/2068 and FNV-1a-32 transfer checksums
1345242802/840162778/2912629838. Each full values transfer samples exactly 512
getters; metadata transfers sample none. The binary files retain their existing
64-bit schema fingerprints; the transfer checksum is a separate test instrument.

Median core cycles per operation (baseline to current):

| Operation | O2 | Os |
|---|---:|---:|
| Schema, first group | 14957.71 -> 9511.04 | 21088.50 -> 11442.44 |
| Schema, last group | 104480.39 -> 5647.78 | 222359.13 -> 6687.19 |
| Commands, first group | 14198.13 -> 9493.57 | 20315.77 -> 11552.13 |
| Commands, last group | 460447.01 -> 9467.93 | 659407.91 -> 11552.13 |
| Values, first group | 280.21 -> 279.59 | 422.15 -> 319.43 |
| Values, last group | 2799.28 -> 235.06 | 4955.15 -> 264.21 |
| Sequential enum visitor | 122.14 -> 123.19 | 312.15 -> 314.21 |
| Sequential parameter visitor | 721.14 -> 698.37 | 813.52 -> 861.20 |

Last-group schema/commands/values reads improve by about 18.5/48.6/11.9 times
at O2 and 33.3/57.1/18.8 times at Os. First and last metadata requests need not
encode the same byte count: the last schema field reaches EOF before filling
256 bytes. Only the same operation across revisions is compared. These are
warm-cache measurements, not serial throughput or cold-cache latency.

The old sequential pack traversal remains intact, but not every measured call
is faster. Enum traversal is 0.9%/0.7% slower here. Parameter traversal improves
3.2% at O2 and regresses **5.9% at Os**. Os outlines shared parameter factories
now used by both indexed and sequential descriptions; the wrapper also loads the
ops callback once. There is no new per-parameter index lookup or loop. The data
does not isolate the cycle cost of outlining from instruction placement/cache
effects, so neither is assigned the entire difference. This concerns description
visitors, not native command execution or field read/write.

Maximum observed nested PSP use, bytes (baseline to current):

| Operation | O2 | Os |
|---|---:|---:|
| Schema read | 440 -> 416 | 464 -> 416 |
| Commands read | 920 -> 880 | 880 -> 648 |
| Values read | 192 -> 184 | 228 -> 184 |
| Enum visitor | 80 -> 80 | 80 -> 80 |
| Parameter visitor | 408 -> 408 | 408 -> 360 |

The 512-byte stack control reports 512 for every image and fill pattern; all
guards remain intact. Complete benchmark Flash images are 58868 -> 58420 bytes
at O2 and 47184 -> 49864 at Os. This includes test-only indexed-API checks in
current. For comparable application-only sizes use the [resource report](../README.md).

The entire original 65536-byte Flash was restored and read back after the final
run. SHA-256 before and after:
`a5903024dba85fab5121150ca8ad13482f97384aa450aab67413881991fb9456`.
Two subsequent halt/read/resume observations saw execution in original Flash,
advancing CYCCNT and zero CFSR/HFSR. The target was left running.

Validate the retained evidence without a board:

```sh
python tests/resources/h7s/verify.py --self-test
```

CI checks the raw rows, four-image coverage, both fill patterns, transfer results,
input/image identities, calculated summaries and verified restoration. A valid
receipt is the positive control; 23 modified receipts must be rejected.

## H7RS startup stall and MPU control

The first baseline fixture stalled during schema construction, before its UART
command loop. Debugging reached completed groups 0..25 with stable stack and
valid descriptors. That attempt provided no usable timing result. Restoring the
original image required target recovery; it was subsequently verified in full.

ST documents possible system hangs from speculative accesses to the unused
GFXMMU window `0x25000000..0x25ffffff`, including devices without GFXMMU, in
[ES0596 section 2.2.17](https://www.st.com/resource/en/errata_sheet/es0596-stm32h7rxx7sxx-device-errata-stmicroelectronics.pdf).
The copied Cube scaffold left this window in the background Normal memory map.
The resource fixture now explicitly marks its MPU region 2 as Device, shareable,
execute-never and inaccessible before enabling caches. Active RAM and Flash
attributes are unchanged; both benchmark revisions use the same workaround.

The [controlled original-image experiment](mpu-control.json) isolates that setup
change from recompilation and deferred startup. It uses the exact previously
failing binary (SHA-256 `186a1e00d4eb1b614e94c01d48e97d8eb0e5b82eb785c7d90399c949b8818218`).
Only the four-byte BL at `0x08006284` is redirected to a 64-byte stub in previously
unused Flash at `0x0800f000`; all existing functions retain their code and addresses.
The stub programs MPU RNR/RBAR/RASR to `2/0x25000000/0x1005002f`, executes barriers
and tail-calls the original schema constructor. That image completes all 40 timing
windows and 18 stack probes, then the original firmware is restored and verified.
The receipt retains exact patch bytes, assembly and UART output. No option bytes
or external memory are altered.

This establishes that the MPU workaround resolves the observed fixture stall.
The actual speculative bus transaction was not traced. The workaround belongs
to platform startup, not to portable telemetry/resource code. The original COBS
project is unchanged; its existing firmware was restored exactly.
