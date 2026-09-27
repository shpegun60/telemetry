# Compile-time local object budget

Authors: Ruslan Kovtun (shpegun60), codexAi. License: MIT.

Measured 2026-09-27 on NUCLEO-H7S3L8, 600 MHz Cortex-M7, 32 KiB D-cache,
CubeIDE ARM GCC 14.3.1, O2/Os, no LTO. Natural runtime-entry alignment and
24/20/24-byte Field/Command/Service entries are unchanged. These are H7S
fixture measurements, not H753 measurements or an application latency bound.
This report preserves the storage-policy snapshot. The later
[dispatch experiment](DISPATCH_RESULTS.md) changes Field entry layout and
removes unused-Workspace checks; its source hashes and results are separate.

## Implemented policy

`TELEMETRY_STRUCTURED_LOCAL_BYTES` defaults to **32** and is exposed as
`telemetry::structured::maxLocalObjectBytes`. Field values and Command
requests with `sizeof(T) <= budget` use local objects; larger ones use
caller-owned Workspace. Zero forces Workspace even for scalar Fields.
The choice is `if constexpr`, without runtime classification. Typed native
calls do not pass through this policy or through the codec.

Service first assigns a fitting Request locally, then tests the actual
`ServiceResult<Response>` against the remaining budget. For example a
16-byte Request and its 20-byte Result do not both fit a 32-byte budget.
One object can be local while the other uses Workspace. Scratch metadata
counts only Workspace objects, including their alignment margins.

The default 32 was selected after comparing all four budgets. It covers
Field values and Command requests through 32 bytes; it is not a claim that
32 is the fastest budget for every DTO. Applications can select another
nonnegative byte budget consistently for the entire binary. The budget is
included in the compiled adapter ABI tag. Archived Local32 images explicitly
set the same budget; changing the default does not rewrite their receipts.

The budget limits simultaneous library payload object sizes, **not total
stack**. Register saves, alignment padding, helper frames, the caller's byte
buffers and user callbacks add their own stack. Large by-value application
parameters still have their ordinary C++ calling-convention cost.

## Measurement contract and evidence

The [common harness](README.md#measurement-contract) times complete calls,
including the loop, indirect probe and checksum consumption. There are five
windows of 16384 operations per case. Array indexing is O(1): two bounds
checks and `catalogs[group].entries[position]`; no traversal or name search.

Both old and new operations are present in every image. The old library is
unchanged. Both consume the same four LE bytes; the old adapter constructs
a Scalar for Command, while the new Command accepts a real `Request{u32}`.
The old library has no comparable aggregate or Service endpoint.

- [Initial budget experiment](storage-before-receipt.json): 8 images, 3760 windows.
- [Retained implementation](storage-receipt.json): 8 images, 3760 windows.

Both runs backed up, restored and read-back verified all 64 KiB of internal
Flash. Receipts identify exact binaries, compiler, library snapshots and
raw measurements. The second run includes removal of trivial-object zeroing,
4-byte-aligned write/command results and shared Service codec preflight.
All images ran mixed and 4 KiB correctness checks before measurement.

## Same-image comparison with the old library

Random access among RAM1024 entries, median cycles/call. New uses budget 32.

| Operation | Old O2 | New O2 | Old Os | New Os |
|---|---:|---:|---:|---:|
| u32 encoded read | 60.04 | 86.23 | 86.04 | 70.23 |
| u32 encoded write | 42.04 | 70.03 | 46.05 | 64.47 |
| Command carrying u32 | 42.00 | 55.09 | 46.00 | 71.23 |
| Service Request/Response | — | 87.83 | — | 105.81 |

Local storage improves the new encoded path. It does **not** make all its
operations faster than the old scalar adapter. Native direct/local/global
codegen still matches byte for byte in the eleven Field/Command probes and
the Service probe at O2/Os.

## Budget comparison

RAM1024 shuffled access; cells contain **O2 / Os** cycles/call.

| Budget | u32 read | u32 write | Command Request{u32} | Service |
|---|---:|---:|---:|---:|
| 0 | 92.89 / 100.88 | 85.13 / 96.41 | 70.09 / 107.23 | 118.78 / 172.77 |
| 16 | 85.28 / 70.30 | 69.15 / 64.48 | 55.09 / 70.23 | 87.84 / 104.82 |
| 32 | 86.23 / 70.23 | 70.03 / 64.47 | 55.09 / 71.23 | 87.83 / 105.81 |
| 64 | 86.23 / 70.23 | 70.03 / 64.47 | 55.09 / 71.23 | 87.83 / 105.81 |

The u32 cases already fit budget 16, so larger budgets add no new storage
optimization there. Binary placement still changes timing: old O2 controls
in the budget-0 image measure 54.05/50.04/42.01 cycles, versus
60.04/42.04/42.00 in budget-16. Cross-image differences are therefore not a
pure additive measurement of `reserve()` cost.

Structures contain `std::array<u32, N/4>` and use a one-entry Flash catalog
with runtime ID. The entire payload is decoded/encoded; output barriers
retain middle bytes too. Service adds each input word to the current value.
These cases have no old-library structural equivalent. Cells are O2 / Os.

| Object bytes | Budget | Field read | Field write | Command | Service |
|---:|---:|---:|---:|---:|---:|
| 16 | 0 | 112 / 222 | 96 / 183 | 94 / 172 | 192 / 418 |
| 16 | 16 | 88 / 184 | 72 / 144 | 70 / 133 | 159 / 366 |
| 16 | 32 | 88 / 183 | 71 / 143 | 69 / 132 | 157 / 422 |
| 16 | 64 | 88 / 166 | 70 / 143 | 69 / 132 | 126 / 310 |
| 32 | 0 | 244 / 281 | 103 / 226 | 102 / 212 | 443 / 589 |
| 32 | 16 | 238 / 271 | 102 / 215 | 101 / 203 | 443 / 571 |
| 32 | 32 | 176 / 245 | 83 / 185 | 81 / 173 | 347 / 535 |
| 32 | 64 | 207 / 214 | 83 / 184 | 81 / 174 | 409 / 449 |
| 64 | 0 | 376 / 410 | 128 / 303 | 128 / 291 | 715 / 914 |
| 64 | 16 | 368 / 395 | 129 / 294 | 127 / 283 | 715 / 950 |
| 64 | 32 | 306 / 401 | 128 / 294 | 126 / 283 | 584 / 884 |
| 64 | 64 | 337 / 305 | 114 / 265 | 115 / 253 | 688 / 696 |

Do not read this as a universally monotonic speed curve. A different budget
changes helper sharing and linked placement, including paths whose storage
choice stays the same. For instance a 32-byte Field is local in both the
32 and 64 builds. At O2 its write stays near 83 cycles while read differs.
These are measured whole-image effects, not a runtime threshold branch.

## Remaining work per encoded call

### Successful u32 write instruction path

An explicit path count in the retained **Local32/O2** image gives the
following Thumb instruction counts. This is disassembly analysis under the
fixture's known successful conditions, not an instruction-counter reading.
It excludes the measurement loop and includes the indirect call and returns.

| Part | Old scalar adapter | New encoded adapter |
|---|---:|---:|
| Runtime-index wrapper, including result consumption | 36 | 34 |
| Selected setter/encoded thunk | 9 | 19 |
| Total successful path | **45** | **53** |

Conditions: valid u32 ID, writable U32 without custom limits, matching Scalar
tag in the old path, four-byte input, nonempty disjoint Workspace. The input
is in AXI RAM above the DTCM stack Workspace, so the first ordered overlap
comparison exits successfully. The linker map places `_estack` at 0x20010000.
For reproducibility, instruction address ranges in `benchmark.asm` are:

- Old wrapper: 0x080048c0..0x08004902, 0x0800494c..0x08004950,
  0x0800495e..0x0800496c; setter: 0x080058c0..0x080058d0.
- New wrapper: 0x08003c38..0x08003c92; thunk: 0x08004cb0..0x08004cbc,
  0x08004cca..0x08004cd6, 0x08004cde..0x08004cec.

The old setter loads the Scalar tag, takes the matching-type branch, loads
the word and stores it to the owner. It performs no numeric conversion on
this path. Removing Scalar therefore does not remove a costly conversion
engine from this particular call. The new thunk instead checks byte length
and overlap, loads its owner from the definition, and handles span/result
ABI bookkeeping. It has no Workspace reservation or initial zeroing here.

This also exposes the comparison's scope: both adapters consume the same
four LE bytes, but the old adapter gets a statically sized array while the
erased new endpoint receives a runtime-sized span. The old adapter has no
Workspace overlap contract to check. The table compares usable operations
under those contracts, not identical sets of boundary checks.

Measured warm-Flash cost is 42 versus 63 cycles; shuffled RAM1024 is 42.04
versus 70.03. Eight additional instructions are **not** an explanation of
exactly eight additional cycles: dependent loads, stack transfers, dual issue
and cache/code placement affect timing. No per-instruction cycle attribution
has been established. The unused-Workspace overlap check in a fully local
thunk was a concrete optimization candidate under the earlier non-overlap
contract. The subsequent dispatch experiment removes that restriction for
unused Workspace and supports in-place Service input/output; this receipt
continues to describe the earlier implementation.

### Resolved-entry controls

Budget-16 Flash/repeated-ID controls, O2:

| Route | Read | Write | Command |
|---|---:|---:|---:|
| Runtime ID and complete encoded operation | 81 | 62 | 55 |
| Entry selected before the measured window | 71 | 55 | 56 |

An already selected Command entry is not measurably faster in this fixture.
At Os the read control is even slower (71 versus 65), because outlining and
register allocation differ. These controls are not subtractable exact stage
costs. They show why removing O(1) indexing alone cannot erase all overhead.

Disassembly identifies the remaining operations:

1. Input/output lengths and scratch overlap checks. Bool-containing types
   also need canonical representation validation before a callback.
2. Entry-to-definition-to-owner loads and the erased function-pointer call.
   The callback body itself is inlined into the known-target thunk where possible.
3. Passing a span's pointer/length and returning dispatch/application status.
   The 8-byte read result also carries written bytes and uses ARM aggregate
   return storage. The write/command result now fits an aligned register word.
4. Looped array decoding/encoding and helper calls retained at Os. The library
   does not reinterpret the byte input as a C++ object or copy struct padding.

The initial local array decoder used `T{}` and emitted an Os `memset` before
overwriting every member. It now uses default-initialization for trivial
types. Nontrivial DMI objects still use neutral construction with explicitly
supplied members, without running application initializers. Public standalone
codec validation remains complete; endpoint adapters reuse the validated steps.

## Stack and image size

Example **individual Command thunk frames**, O2 / Os bytes:

| Request object | Workspace (budget 0) | Local (fitting budget) |
|---:|---:|---:|
| 16 | 24 / 56 | 40 / 56 |
| 32 | 24 / 56 | 56 / 72 |
| 64 | 24 / 56 | 88 / 104 |

These exclude caller/helper/callback frames and interrupt nesting. They must
not be used as full call-chain maxima. Current 4 KiB read/write/command thunks
remain bounded: O2 32/16/16 B, Os 32/24/24 B, Og 112/88/88 B. Large raw/wrapped
Service response thunks are 40/40 B at O2, 64/64 B at Os and 80/80 B at Og.

Whole benchmark image bytes (includes HAL, UART, old/new fixtures and checks):

| Budget | O2 | Os |
|---|---:|---:|
| 0 | 52668 | 52588 |
| 16 | 51164 | 51180 |
| 32 | 50812 | 50828 |
| 64 | 50524 | 50476 |

## Reproduce and validate

Use the resolved CubeIDE compiler/programmer paths from the [harness README](README.md).

```powershell
python tests/structured/endpoints/h7s/run.py --cube path/to/h7s_cobs_test --arm-cxx $armCompiler --programmer $programmer --output build/storage-run --variants Local0 Local16 Local32 Local64 --legacy --components --storage-probes --run
python tests/structured/endpoints/h7s/verify.py --receipt tests/structured/endpoints/h7s/storage-receipt.json --artifacts build/storage-policy-final --self-test
python tests/structured/endpoints/storage.py --cxx g++ --build-dir build/storage-check
```

The storage correctness matrix covers 0/16/32/64 on host/sanitizers and ARM
O2/Os/Og, DMI, exact/over-budget sizes, bool validation, skewed alignment,
4 KiB objects, nested leases, mixed Service storage and cross-TU budget mismatch.
Offline receipt validation has 19 mutation checks; it does not run hardware.
CI includes both correctness and receipt validation. Full application call-chain
stack and final linked no-heap evidence remain the separate Stage 13 gate.
