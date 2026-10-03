# Stage 14 probe preparation: host and ARM codegen

Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.

The user deferred the new hardware run on 2026-10-03. This suite executes
prospective MCU probe bodies on a host and compiles/links them for Cortex-M7.
Its runner has **no serial, programmer or device-discovery integration**.
The attached board is not used. DWT cycles and whole-call-chain stack
watermarks are not established by these results; Stage 14 hardware evidence
remains a separate task.

## Reproduce without hardware

```sh
python3 tests/structured/mcu/run.py --cxx g++ --build-dir build/mcu-host
python3 tests/structured/mcu/run.py --cxx g++ --null-checks --build-dir build/mcu-host-null
python3 tests/structured/mcu/run.py --cxx clang++-18 --sanitize --build-dir build/mcu-host-san
python3 tests/structured/mcu/run.py --arm --cxx arm-none-eabi-g++ --build-dir build/mcu-arm
python3 tests/structured/mcu/run.py --arm --null-checks --cxx arm-none-eabi-g++ --build-dir build/mcu-arm-null
```

The default profiles are O2/Os/Og. `--optimizations O2 Os` selects a subset.
Every invocation records actual compiler/flags, command results, individual
stack frames and normalized input hashes. An input change during the run
rejects the result. ARM additionally captures linked sections, symbols and
probe-object disassembly. Generated evidence stays in the chosen directory.

## What is checked

- **Mixed:** the unchanged 97-check multi-TU consumer, small Config Fields
  and Services through direct/local/global/native/encoded APIs, a bound
  OwnerSlot, two populated catalog groups, large Field reads and actual
  4 KiB Command requests and Service request/response pairs. `successFrom`
  constructs the response without a second large convenience temporary.
- The large Service output is compared against **all 4096 input bytes**,
  including in-place input/output. Insufficient scratch is refused before
  the callback, and leases release the Workspace after each path.
- Streaming descriptor slices at **every byte offset** are checked against
  packed descriptor bytes: status, written length, next cursor, eof and data.
  Full values reads include the existing mixed scalar/enum/array/struct table.
- **Scale:** 128 distinct method targets share one owner whose values differ
  by position. Direct indexed access, one named visitor, one lambda visitor,
  readAs, encoded access and unchanged legacy scalar access must return the
  right value for same/sequential/shuffled IDs. Known-position probes run
  the same-ID profile only. Invalid entry/group/max-u32 IDs are refused.

The ID sequences live in explicit test storage, outside the endpoint stack.
Metadata stays immutable. ModelView is cached for the probe roots, as a
transport consumer can do, rather than reconstructed for every operation.
The old library is included solely as a comparison; no legacy source changed.

## Codegen evidence and its boundaries

Optimized local/global typed bodies must equal the direct body, or contain
only an explicitly resolved compiler tail forward to that equivalent body.
The report preserves both **raw** and **effective** comparisons and the actual
forward target. A tail forward is an extra branch, not an instruction alias.
Two parser positives and five mutation controls check that different code,
missing targets and forwarding cycles cannot pass this accounting.

`-fno-delete-null-pointer-checks` is recorded separately. GCC can retain the
existing target-address check even for a defined member function. Local/global
implementations must still agree; their difference from direct C++ is visible
in the report. No claim of zero extra instructions is made for that mode.
Og records native streams without requiring optimized instruction identity.

Linked ARM probes reject allocation symbols and startup constructor sections.
The shared [symbol/count gates](../qualification/gates.py) include newlib
`_name_r` allocation and printf families, with deliberately retained-symbol
controls. Mixed must execute exactly 12,230 conditions, including the separately
reported 97-check consumer; Scale must execute exactly 2,831. A smaller positive
count fails the run. Gate controls are reported separately from C++ conditions.
ARM links a separate negative ELF against real newlib and runs real `nm` on
it. `_malloc_r` and `_printf_r` must both be retained definitions and refused.
Internal `svfprintf/svfiprintf/sfvwrite/sprint` and `sbrk` families are included.
The negative ELF's hash and rejected names are saved; CI retains its ELF and
command/nm logs. That control has its own libc/libnosys link group and is not
an endpoint image or an executed program.
An explicit ABI check belongs in main; no such call may enter the probe TU.
New structured frames remain at most 256 B, except the unchanged qualification
roots with their 512 B O2/Os and 768 B Og budgets. These are **individual
compiler frames**, not an established maximum call-chain stack.

Frozen legacy frames are reported separately. In particular, the CubeIDE
14.3.1 Og scalar read control has a 1984 B frame; it is not a hidden large
object in the new structured path. Existing legacy gates are not changed.

MinGW host scaling omits per-function named sections, following the Stage 13
COFF section-name limit already documented there. ARM retains those sections,
GC and linked inspection. Host executable sizes are not presented as MCU Flash.

## Local results, 2026-10-03

All final runner inputs matched their captured hashes: **187 files**. The
runtime counts below are actual C++ conditions, including repeated profiles;
they are not counts of compiler invocations or distinct test scenarios.

| Configuration | Successful commands | Executed conditions | Failures |
| --- | ---: | ---: | ---: |
| MinGW GCC 13.1, O2/Os/Og | 58 | 45,183 | 0 |
| Linux GCC 13.3, null-check mode, O2/Os/Og | 58 | 45,183 | 0 |
| Clang 18.1.3, ASan/UBSan, O2/Os/Og | 58 | 45,183 | 0 |
| CubeIDE ARM GCC 14.3.1, O2/Os/Og | 73 | not executed | — |
| CubeIDE ARM GCC 14.3.1, null-check mode | 73 | not executed | — |
| ARM GCC 13.2.1, O2/Os/Og | 73 | not executed | — |
| ARM GCC 13.2.1, null-check mode | 73 | not executed | — |

Mixed executes 12,230 conditions per optimization; Scale executes 2,831.
The parser's two positive/five negative controls are counted separately.
The freestanding fixture gives abort a deterministic trap implementation;
it does not link newlib's process/signal path to implement failed contracts.
The linked allocation-symbol check remains in force.

The tracked [local receipt](local-receipt.json) records seven local runs:
actual compiler/flags, the shared LF input digests, raw report hashes, results,
selected individual frames, native comparisons and real-symbol controls.
`source_head` identifies the pre-publication checkout; `source_dirty` describes
the captured source set, including new files, rather than unrelated review work.
It is a durable local record, not an independent CI or hardware execution.
Raw reports remain in the chosen build directories; their hashes allow later
comparison but do not reconstruct missing report/log content.

```sh
python3 tests/structured/mcu/receipt.py verify tests/structured/mcu/local-receipt.json --self-test
python3 tests/structured/mcu/receipt.py capture --report NAME=build/mcu-host/report.json --report ARM=build/mcu-arm/report.json --output build/local-receipt.json
```

Capture requires complete O2/Os/Og reports with exactly matching current inputs.
Verification compares the recorded input set with current sources; mutation
controls refuse changed scope, counts, hashes, controls and large probe frames.

CubeIDE 14.3.1 normal O2/Os direct/local/global probes have identical normalized
bodies: **three instructions for u32**, **six for Config Field/Service**,
excluding literal pool words and alignment padding. GCC 13.2.1 Os uses a
one-branch global-to-local forward for the u32 probe; the raw difference is
retained and is not described as instruction identity.

Measured CubeIDE individual frames:

| Probe | O2 | Os | Og |
| --- | ---: | ---: | ---: |
| 4 KiB encoded Field read | 192 B | 168 B | 168 B |
| 4 KiB encoded Command request | 176 B | 152 B | 152 B |
| 4 KiB encoded Service request/response | 216 B | 192 B | 192 B |
| 128-target named visitor | 16 B | 16 B | 16 B |
| 128-target readAs | 16 B | 16 B | 16 B |
| Encoded u32 read | 40 B | 32 B | 56 B |
| Legacy scalar u32 read | 32 B | 32 B | 1,984 B |

The legacy GCC 13.2.1 Og comparison frame is 1,992 B. The structured limits
do not reinterpret either legacy measurement as a new-core regression.

Full CubeIDE linked fixture sections; these include both test setup and all
retained probe paths, not an isolated library Flash measurement:

| Fixture/profile | text | rodata | data | bss |
| --- | ---: | ---: | ---: | ---: |
| Mixed O2 | 17,304 B | 6,792 B | 76 B | 57,920 B |
| Mixed Os | 14,464 B | 6,928 B | 76 B | 57,920 B |
| Mixed Og | 25,152 B | 7,080 B | 76 B | 57,920 B |
| Scale O2 | 16,808 B | 19,404 B | 0 B | 1,032 B |
| Scale Os | 17,576 B | 19,372 B | 0 B | 1,032 B |
| Scale Og | 34,848 B | 19,444 B | 0 B | 1,032 B |

Mixed bss explicitly includes the existing qualification buffers, the new
16 KiB output and 4 KiB input, and shared scratch. Scale rodata includes the
128-row legacy aligned table as well as the new native and encoded tables.
The fixtures intentionally have no vendor startup/HAL code; their ELF files
are inspected offline and are not images to program into a board.

## Publication boundary

Stage 13 exact-SHA [CI 37122588689](https://github.com/shpegun60/telemetry/actions/runs/37122588689)
completed successfully for `7266a93`: 9/9 jobs. This new probe suite is an
additional slice. Its [CI 37130246477](https://github.com/shpegun60/telemetry/actions/runs/37130246477)
completed successfully for `f67a5f9`: 9/9 jobs, including all selected host MCU
steps and the ARM MCU step. The subsequent real-symbol controls and local
receipt have their own publication CI. Prior H7S receipts do not prove
execution of these new probe bodies.
No host watermark is presented as ARM stack evidence. Large encoded Field and
Service paths use caller-owned Workspace; a separate native `readAs<Big>`
stack/cycle comparison and actual call-chain watermarks remain in Stage 14's
execution qualification, rather than being inferred from these `.su` frames.
