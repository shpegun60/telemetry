# Maintained review regressions

Authors: Ruslan Kovtun (shpegun60), codexAi. MIT.

These checks promote the six technical reviews from September 25, 2026 and
the three follow-up reviews from September 26 into
the ordinary runners. `tests/review/` and `doc/Review-2026-09-25/transcripts/`
remain the original evidence; they are not silently rewritten to describe fixes.
The critic's architecture/product proposals are outside this change.

Run `python tests/run_checks.py --build-dir build/checks` for C++17, or add
`--std c++20`; Clang supports `--sanitize`. This runs the 15 existing core
suites plus eleven additional single-source suites, a separately compiled
JSON linkage check, and 18 single-type builds of FieldParityCheck.
Each type runs **4320** comparisons, preserving all **77760** comparisons while
bounding compiler time and memory. Three types per build exceeded the 180-second
command timeout on a CI sanitizer runner. The single-type builds retain that
timeout, every binding form, every limit case and the same optimization flags.

| Maintained check | Original review source / contract |
|---|---|
| NumericEdges, HonorFlagsCheck | numeric-core ConversionEdges/HonorFlagsProbe; 328 edge checks, forbidden FP modes |
| FieldParityCheck (types 1–18) | fields NativeDynamicParity; native and erased paths, 18 types, six binding forms |
| OwnerLifetimeCompileFail, PointerOwnerCompileFail | fields conversion temporaries and pointer-like owners; positive lvalue controls |
| BorrowedBraceCompileFail | 72 rejections and 52 executed controls: explicit const types, braces, proxy conversions, mixed getter/setter pairs, array contexts and slot Target lifetimes |
| CommandArityCheck | commands ArityMatrix; 46 order/side-effect checks |
| JsonBoundaryCheck | catalog-json JsonBoundaryProbe; every buffer size, both integer modes |
| SlotEdgesCheck, NullChecksFlag | slot presence/lifetime edges; runtime weak/null pointer handling and the explicitly limited GCC slot-only no-delete control |
| SlotCallableCheck, SlotCallableCompileFail | ordinary overload resolution, forwarding and cv preservation; 25/26 runtime checks and 15 rejections |
| SetterConversionCheck, NativeSetterCodegen, BoundSetterCodegen | 138 checks across nine native binding forms, including every slot kind; inspect the actual erased setter thunks as well as typed wrappers |
| WeakTargetCompileFail | 11 weak NTTP rejections, rather than calling an unresolved address |
| IdBoundaryCheck, IdBoundaryCompileFail, IdBoundaryCodegen | 122 runtime checks, 104 rejections, six abort cases, ARM32 high-word branch and a mutated-register control |
| DefinitionNamesCompileFail | eight missing required field/unit/group label rejections |
| JsonLinkageCheck | four shared JSON helper addresses agree across separate translation units; catches TU-local linkage even when output bytes agree |
| ReviewCheck | widened runtime IDs, checked manual setters, genuine zero fingerprint, empty metadata, reference slots |
| ReviewCompileFail | 25 exact-diagnostic contract failures plus a valid control |
| RuntimeLimitsAbort, RuntimeMetadataAbort | 13 intentional abort cases, including missing field/group/command labels and wide makeId input |
| AbiGcSections, AbiRetention | matching/mismatching emitted and namespace anchors survive section GC/LTO; compiler-omitted code remains an explicit control |

The core runner verifies **262 review compile-fail cases**, eight successful
case-zero controls and the slot-only `-fno-delete-null-pointer-checks` check.
The ARM runner executes the same
compile-contract matrix, so host width cannot conceal 32-bit narrowing.
Clang independently tests `-fno-honor-nans` and `-fno-honor-infinities` at O1/O2,
even with that particular warning disabled on the command line. Global `-w`
and system-header suppression can hide the diagnostic; those compiler modes
remain unsupported, as do per-function fast-math assumptions. Full sanitizer
runs use Clang. GCC's full UBSan build still rejects some constexpr function
addresses; explicit `-fdelete-null-pointer-checks` is not a complete workaround.

`NativeSetterCodegen` checks four emitted native-pointer invokers, and
`BoundSetterCodegen` checks nine method/free/callable/slot adapters. CubeIDE GCC
14 emits all nine without stack work or non-tail calls at O2 and Os; GCC 13 does
the same at O2. GCC 13 Os still outlines `std::get_if` in seven adapters, with
16-byte frames (four were 8 bytes before checked conversion was added). The
runner pins this measured compiler-specific cost, instruction ceilings and tail
conversion branches; a same-size instruction mutation must fail the frame gate.
This is generated-code evidence, not a cycle measurement.

`tests/resources/run.py` adds MetadataContractCheck, CursorCheck and
EmbeddedReviewCheck to its ordinary host/sanitizer jobs. Resource definitions
have 34 diagnostic-checked failures plus 34 executed path/provider binding controls.
LIST prefix preservation, in-place packets and empty final WRITE are part of
CoreCheck. The browser decoder runs against unchanged v2.1 goldens.

## H7S execution

The shared [EmbeddedReviewCheck.hpp](EmbeddedReviewCheck.hpp) performs **3328**
assertions on both host and Cortex-M7, including executed 64-bit conversion
boundaries, non-finite values, wide IDs without callback side effects, reference
slots, every byte of schema/commands/values 2.1, getter counts and EOF replay.
Metadata chunks include 1/2/3 bytes; each live value remains an atomic token.

```powershell
python tests/regression/h7s/run.py --cube <copied-scaffold> --arm-cxx <CubeIDE-g++> --output <new-output-directory> --run
python tests/regression/h7s/verify.py --self-test
```

Without `--run`, the script only builds. It checks the NUCLEO-H7S3L8 identity,
backs up all 64 KiB of internal Flash before programming, limits both images
to that bank, and restores and reads back the backup in `finally`. It never
changes option bytes, external memory or the original COBS scaffold. The
fixture includes the established H7RS GFXMMU speculative-access workaround.

The [original receipt](h7s/receipt.json) and
[follow-up receipt](h7s/followup-receipt.json) pin compiler, ELF/binary/object hashes,
library and fixture inputs, both O2/Os reports, and identical backup/readback
hashes. CI checks its shape and 11 deliberately invalid mutations. This is
archived measurement evidence, not a claim that CI has a connected board.

## Contract boundaries

Borrowed views/owners must remain alive. Direct rvalue calls and converting
temporary bindings are rejected, but a user helper accepting `const T&` can
hide the original lifetime; `std::data(temporaryTable)` has the same issue.
The library does not own these objects.

An emitted ABI anchor now retains its exact link dependency through section
collection on the tested ELF toolchains. `TELEMETRY_RETAIN_ABI()` at namespace
scope also preserves participation when the compiler would omit an unused
internal function; it is an explicit per-TU opt-in. Header-only inclusion or an
unextracted archive member still makes no such promise. Matching/mismatching
links run with section GC and LTO on host and ARM, without field hot-path work.
All modules still need consistent layout settings and a clean rebuild after an
ABI change. ARM retained anchors currently require non-PIC builds.

For guaranteed static component diagnostics use `makeId<Group, Entry>()`.
An invalid ordinary `makeId(0, 65537)` call may compile and then abort, including
before main; `tryMakeId()` is the fallible runtime API. Global packed IDs accept
integers; local enum positions must first be paired with a group. Extract IDs
from atomics/wrappers explicitly without narrowing the original value.
