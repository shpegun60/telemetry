# Maintained review regressions

Authors: Ruslan Kovtun (shpegun60), codexAi. MIT.

These checks promote the six technical reviews from September 25, 2026 into
the ordinary runners. `tests/review/` and `doc/Review-2026-09-25/transcripts/`
remain the original evidence; they are not silently rewritten to describe fixes.
The critic's architecture/product proposals are outside this change.

Run `python tests/run_checks.py --build-dir build/checks` for C++17, or add
`--std c++20`; Clang supports `--sanitize`. This runs the 15 existing core
suites plus six additional suites and six groups of FieldParityCheck.
Splitting the 18 types into groups preserves all **77760** comparisons while
keeping sanitizer compilation memory bounded. It does not remove any binding
form, limit case or optimization flag.

| Maintained check | Original review source / contract |
|---|---|
| NumericEdges, HonorFlagsCheck | numeric-core ConversionEdges/HonorFlagsProbe; 328 edge checks, forbidden FP modes |
| FieldParityCheck (groups 1–6) | fields NativeDynamicParity; native and erased paths, 18 types, six binding forms |
| OwnerLifetimeCompileFail, PointerOwnerCompileFail | fields conversion temporaries and pointer-like owners; positive lvalue controls |
| CommandArityCheck | commands ArityMatrix; 46 order/side-effect checks |
| JsonBoundaryCheck | catalog-json JsonBoundaryProbe; every buffer size, both integer modes |
| SlotEdgesCheck, NullChecksFlag | slots presence/lifetime edges and GCC NTTP null-check portability |
| ReviewCheck | widened runtime IDs, checked manual setters, genuine zero fingerprint, empty metadata, reference slots |
| ReviewCompileFail | 25 exact-diagnostic contract failures plus a valid control |
| RuntimeLimitsAbort, RuntimeMetadataAbort | 8 intentional abort cases, including null command labels and wide makeId input |
| AbiGcSections | matching/mismatching live anchors and unused ELF section collection |

The core runner verifies **52 new compile-fail cases**, three successful
controls and `-fno-delete-null-pointer-checks`. The ARM runner executes the same
compile-contract matrix, so host width cannot conceal 32-bit narrowing.
Clang independently tests `-fno-honor-nans` and `-fno-honor-infinities` at O1/O2,
even with their diagnostic disabled on the command line.

`tests/resources/run.py` adds MetadataContractCheck, CursorCheck and
EmbeddedReviewCheck to its ordinary host/sanitizer jobs. Resource definitions
have 14 diagnostic-checked failures plus positive provider/base-class controls.
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

The [receipt](h7s/receipt.json) pins compiler, ELF/binary/object hashes,
library and fixture inputs, both O2/Os reports, and identical backup/readback
hashes. CI checks its shape and 11 deliberately invalid mutations. This is
archived measurement evidence, not a claim that CI has a connected board.

## Contract boundaries

Borrowed views/owners must remain alive. Direct rvalue calls and converting
temporary bindings are rejected, but a user helper accepting `const T&` can
hide the original lifetime; `std::data(temporaryTable)` has the same issue.
The library does not own these objects.

An ABI anchor must be referenced from retained code. ELF section collection
can discard an unused anchor; the test demonstrates that limitation. The
MinGW PE linker diagnoses the unused mismatching reference as well. This is
not an automatic per-TU retention mechanism. All modules still need consistent
layout settings and a clean rebuild after an ABI change.
