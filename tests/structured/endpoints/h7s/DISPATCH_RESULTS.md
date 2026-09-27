# Encoded dispatch boundary and direct contexts

Authors: Ruslan Kovtun (shpegun60), codexAi. License: MIT.

Measured 2026-09-27 on NUCLEO-H7S3L8, 600 MHz Cortex-M7, 32 KiB D-cache,
CubeIDE ARM GCC 14.3.1, O2/Os without LTO. The local object budget is **32 B**
throughout. These are fixture measurements, not a claim about every application.
The old scalar library is unchanged. Native typed APIs, codec representation
and O(1) positional ID lookup are unchanged by this experiment.

## Retained changes

1. An entirely local endpoint does not access Workspace at all, including
   overlap/capacity checks. Endpoints that use scratch still reject overlap
   between wire buffers and Workspace before invoking the callback.
2. Checked entry methods validate byte lengths once, after index lookup.
   Internal thunks receive context, raw byte pointers and Workspace, without
   runtime span lengths. Their generated types know exact wire extents.
3. Method/callable/slot contexts point directly to the owner/callable/slot.
   Runtime function pointers remain in a binding object, avoiding any
   function-pointer-to-object-pointer conversion. Custom bindings keep their
   snapshot hook. Field needs separate contexts for independent read/write
   bindings: its ARM entry grows from 24 to **28 B**; Command/Service remain
   **20/24 B**, all naturally aligned to 4. Definition storage is additional.
4. Service permits overlapping input/output. It decodes the whole Request
   into independent native storage before the callback and before encoding
   any response byte. No pointer-overlap check between input/output is needed.
   Workspace overlap is still rejected if scratch is used. This applies to
   complete and partial input/output sharing, including mixed storage policies.

`FieldEntry::readEncoded/writeEncoded`, `CommandEntry::executeEncoded` and
`ServiceEntry::callEncoded` are the checked resolved-entry APIs. Raw function
pointers inside entries are internal operations with prevalidated buffers.
Names, immutable tables, borrowing/lifetime rules and one-snapshot slot
behavior remain unchanged. Structured ABI revision 5 covers the changed
thunk signatures and entry layout, including both Field contexts.

The four phases were built from isolated library snapshots. The final phase
also includes removal of Service input/output overlap checking, so its Service
delta is not an isolated measurement of context indirection alone.

## Board results

Five 16,384-call windows per case; median cycles/call including the same
measurement loop, indirect probe and checksum. Caches are reset and warmed
before every window. RAM1024 IDs use the same precomputed shuffle for all
variants. Both libraries operate on the same Device. The old command takes
a u32; the new command takes a real `Request { u32 }` as required by the API.

| RAM1024 shuffled, O2 | Before | No unused Workspace check | Raw pointer thunk | Direct context (retained) |
|---|---:|---:|---:|---:|
| Read u32 | 86.23 | 73.17 | 69.90 | **65.01** |
| Write u32 | 70.03 | 66.16 | 61.61 | **55.01** |
| Command | 55.09 | 44.09 | 41.75 | **34.63** |
| Service | 87.83 | 69.77 | 56.59 | **52.25** |

| RAM1024 shuffled, Os | Before | No unused Workspace check | Raw pointer thunk | Direct context (retained) |
|---|---:|---:|---:|---:|
| Read u32 | 70.23 | 63.25 | 59.29 | **51.01** |
| Write u32 | 64.47 | 63.89 | 50.59 | **42.66** |
| Command | 71.23 | 70.21 | 58.35 | **58.25** |
| Service | 105.81 | 94.78 | 79.29 | **71.00** |

The retained implementation versus the unchanged scalar library in the same image:

| RAM1024 shuffled | O2 old | O2 new | Os old | Os new |
|---|---:|---:|---:|---:|
| Read u32 | 60.06 | 65.01 | 86.09 | 51.01 |
| Write u32 | 42.06 | 55.01 | 46.08 | 42.66 |
| Command | 42.01 | 34.63 | 46.00 | 58.25 |

This is **not a universal speed win over the old scalar library**. For u32
to u32 its existing type check takes the same-type branch; no expensive
numeric conversion occurs. The new encoded route performs byte-boundary
validation. Old read timing also varies slightly with image code placement:
compare each new result to the old control in that same image.

Native old/new read/write/command controls remain equal: 8/9/9 cycles at O2
and 8/11/11 at Os. Eleven direct/local/global Field and Command ARM codegen
probes remain byte-identical at O2/Os. The separate Service model probes also
retain byte-identical direct/local/global calls (24/20 bytes at O2/Os).

Larger objects remain covered. Flash repeated-ID before/retained cycles:

| Object bytes | O2 read | O2 write | O2 command | O2 Service | Os read | Os write | Os command | Os Service |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| 16 | 88 / 74 | 71 / 56 | 69 / 57 | 157 / 152 | 183 / 174 | 143 / 146 | 132 / 134 | 422 / 366 |
| 32 | 176 / 162 | 83 / 68 | 81 / 68 | 347 / 335 | 245 / 219 | 185 / 186 | 173 / 174 | 534.5 / 435 |
| 64 | 306 / 302 | 128 / 124 | 126 / 122 | 584 / 573 | 401 / 346 | 294 / 304 | 283 / 292 | 884 / 809.5 |

The small Os composite-write/command regressions are retained here rather than
hidden by the u32 result. This experiment establishes the particular end-to-end
measurements; it does not assign each cycle to an instruction or cache event.

## Successful u32 write instruction path

Actual linked O2 disassembly, valid ID, exact four-byte payload, nonempty
disjoint Workspace, callback returning Applied. Counts exclude the benchmark
loop and include calls/returns. Raw `.word` literals and unexecuted error
branches are excluded.

| Version | Index/boundary/probe instructions | Selected handler instructions | Total |
|---|---:|---:|---:|
| Old scalar control | 36 | 9 | 45 |
| Before | 34 | 19 | 53 |
| No unused Workspace check | 34 | 14 | 48 |
| Raw pointer thunk | 30 | 6 | 36 |
| Direct context | 29 | **5** | **34** |

The initial 19-instruction handler included span stack bookkeeping, a length
check, Workspace overlap loads/branches, and definition-to-owner loading.
Moving preflight to the boundary also changes register allocation, so the
phase deltas should not be read as independent additive per-feature costs.
The retained handler is:

```asm
mov  r3, r0       // direct owner context
ldr  r2, [r1]     // four canonical input bytes on this LE target
movs r0, #0      // {Ok, Applied}
str  r2, [r3]     // inlined device setter
bx   lr
```

No Workspace access, span spill, type switch, object zeroing or extra owner
load remains in this handler. The checked wrapper still indexes catalogs and
entries, checks writable capability, exact length and scratch requirement,
loads the target/context, calls indirectly and consumes the result. Runtime
metadata loads are unavoidable in this checked interface because the selected
entry's type is unknown to the caller.

**34 instructions versus 45 does not imply fewer cycles.** The O2 write still
takes 55 versus 42 cycles in this fixture. Different branch paths, instruction
placement, dependent memory accesses and issue scheduling have different costs.
Their individual contributions have not been isolated by this measurement.

The linked fixture uses 50,812 -> 49,908 Flash bytes at O2 and
50,828 -> 50,168 at Os. These totals include tables and all control probes;
they are not a stand-alone library footprint.

## Rejected force-inline experiment

Forcing all four checked entry methods inline removed an extra Os call in
resolved-entry probes. It improved resolved read/write/command from 70/61/60
to 49/43/40 cycles. However, RAM1024 O2 command worsened from 34.63 to 43.62,
and the Os 64-byte Service from 809.5 to 930.5. Whole-image sizes grew by
64/32 bytes at O2/Os. That blanket attribute change was reverted; ordinary
inline is retained. These measurements also illustrate why instruction counts
alone cannot select the fastest image.

## Correctness, stack and evidence

`ErasedBoundary.cpp` is run by both endpoint and storage-policy runners.
It tests same-buffer and bidirectional partial overlap for 4/64-byte request
and response combinations, all budgets 0/16/32/64, invalid bool and length,
no output on application failure, direct const/derived contexts, independent
read/write objects, and encoded OwnerSlot availability/rebinding. Existing
binding tests cover functions, callables, all five slots and custom snapshots.
The host matrix passed MinGW GCC 13.1, Clang 18 ASan/UBSan and GCC null-check
mode; ARM compile/codegen checks cover O2/Os/Og. ABI mismatch tests still fail
at the expected compiled adapter symbols. No public codec checks were removed.

CubeIDE 14.3.1 individual 4 KiB Field-read/write/Command thunk frames are
24/12/12 B at O2, 24/16/16 at Os and 80/72/72 at Og. Large raw/wrapped
Service response frames are 32/32, 40/40 and 64/64. Checks moved into a
caller boundary, so these reductions are not full call-chain stack results;
Stage 13 retains that separate obligation.

- [Four-phase receipt](dispatch-receipt.json): 8 images, 3,760 timing windows.
- [Rejected inline receipt](dispatch-inline-rejected-receipt.json): 4 images,
  1,880 windows; its Boundary variant repeats the retained Context result.

Both sessions restored and read-back verified all 65,536 bytes of original
internal Flash. Before/after SHA-256:
`a5903024dba85fab5121150ca8ad13482f97384aa450aab67413881991fb9456`.
Receipts include compiler, source and binary hashes and raw timings. The Git
HEAD recorded in them is the Stage 07 checkpoint; source hashes describe the
uncommitted Stage 08 snapshots actually built. Offline validation is not a
fresh hardware run or proof that a later source edit has been executed.

To run the current implementation plus the unchanged scalar controls:

```powershell
python tests/structured/endpoints/h7s/run.py --cube path/to/h7s_cobs_test --arm-cxx $armCompiler --programmer $programmer --output build/dispatch-new-run --variants Context --legacy --components --storage-probes --run
python tests/structured/endpoints/h7s/verify.py --receipt tests/structured/endpoints/h7s/dispatch-receipt.json --artifacts build/dispatch-phases-h7s --self-test
```

Historical Before/Local/Pointer/Boundary builds additionally require the
isolated snapshot directories passed through `--dispatch-snapshots`; they do
not silently rebuild a historical label from current headers. `Context`
always uses the current library. Omit `--run` for compile-only inspection.
