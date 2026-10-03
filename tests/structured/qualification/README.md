# Stage 13: integrated qualification

Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.

This suite combines the existing heterogeneous/4 KiB model, a separately
compiled metadata provider, two application consumers and all four compiled
encoded boundaries. It adds integration evidence without replacing the
Stage 04–12 type, lifetime, codec, wire, storage and native codegen gates.

## Reproduce

```sh
python3 tests/structured/qualification/run.py --cxx g++ --build-dir build/qualification
python3 tests/structured/qualification/run.py --cxx g++ --null-checks --build-dir build/qualification-null
python3 tests/structured/qualification/run.py --cxx clang++-18 --sanitize --build-dir build/qualification-san
python3 tests/structured/qualification/run.py --arm --cxx arm-none-eabi-g++ --build-dir build/qualification-arm
python3 tests/structured/qualification/run.py --arm --null-checks --cxx arm-none-eabi-g++ --build-dir build/qualification-arm-null
```

The runner captures compiler/flags, command results, measured compile times,
linked sections, individual `.su` frames and normalized input hashes. A change
to an input during the run rejects the result. Expected rejections require
the relevant diagnostic; successful commands are counted separately from
the C++ program's actual checks. Generated files stay in the selected directory.

- The four-TU executable has **97 counted checks**. It covers native
  get/traversal/readAs/writeAs, shared TypeIds, slot reset/rebind, scalar and
  structural encoded calls, short buffers, invalid bool, unavailable targets,
  4 KiB responses, in-place Service payloads, descriptor identity and values.
  Failed checks report their source location on the host.
- Every C++ allocation form used by the consumers terminates. ARM linked
  images additionally reject retained allocation, formatting and legacy Scalar
  symbols under O2/Os/Og, GC, LTO and PIC/PIE. Metadata must have no startup
  constructor section. The explicit ABI module check occurs in main; consumer
  assembly must contain no call to that check.
- Each of four encoded boundaries has a separately linked positive control
  and an exact mismatch control: a 32-byte caller versus a zero-budget Adapter.
  Linux/ARM exercise GC, LTO, PIC and PIE: **16 link rejections**. Windows PE
  exercises GC and LTO: **8 link rejections**. A separate typed-null name
  produces **one compile rejection** in every mode.
- `Names.cpp` uses pointers to constexpr array elements in Field, Command,
  Service and group names. It detects a real GCC null-check-mode gap in
  `cd8b636`: repeated address comparisons outside the already validated Name
  wrapper prevented constant evaluation. The fixed Name checks its input once;
  its non-null/nonempty/UTF-8 invariant is reused by endpoint/group constructors.
  Runtime null names remain refused; layouts and hot invocation code do not change.
- `Scale.cpp` executes every selected row and operation, then refuses invalid
  entry/group IDs. It compares an encoded-u32 baseline, one/two/four distinct
  lambda types and four call sites sharing one named visitor. Rows have unique
  method targets and stable generated names. ARM measures object sections;
  these numbers are not a whole-image Flash delta or MCU cycles.
- `Depth.cpp` records build time for structural depth 4/8/16. Timing is evidence,
  not a machine-dependent pass threshold. The scalar baseline includes the
  existing legacy fixture with the same owner and reports its layouts/bodies.

Host totals include repeated builds/runs: MinGW **2433 checks**, Linux GCC
null-check and Clang ASan/UBSan **2635 checks** each, zero failures. ARM
qualification compiles/inspects/links; it does not report runtime assertions.
The qmake consumer separately runs the same 97 checks and verifies that old
JSON/v2 adapters and the optional protocol example are not selected.

MinGW's 128-row probe with per-function named sections exceeded COFF's textual
section-name string-table offset. Only that host scaling correctness build
disables function/data sections. The real multi-TU consumers retain GC, and
all ARM scaling measurements keep normal function/data-section flags. This
exception is recorded in the runner rather than hidden as a library result.

## CubeIDE GCC 14.3.1 evidence

Mixed linked image sections, with explicit buffers counted in bss:

| Profile | text | rodata | data | bss | typed root frame | encoded root frame |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| O2 | 7384 | 4044 | 76 | 36896 | 136 | 336 |
| Os | 9536 | 4244 | 76 | 36896 | 128 | 336 |
| Og | 17280 | 4340 | 76 | 36896 | 368 | 520 |

The encoded root passes ModelView by value across several compiled boundaries.
Its argument/spill area is not an extra 4 KiB DTO. New fixture budgets are
512 B per root at O2/Os and 768 B at Og; other individual frames remain at
most 256 B. Existing endpoint/codec/visitor frame budgets are unchanged.
This is **not a maximum call-chain stack measurement**. LTO linked sections
are inspected, but pre-LTO `.su` files are not presented as post-LTO frames.

For 128 rows, measured object sections:

| Used visitor call sites | O2 text / rodata | Os text / rodata |
| --- | ---: | ---: |
| Encoded baseline | 3568 / 5274 B | 2088 / 5274 B |
| One lambda type | 5568 / 5760 B | 3988 / 5760 B |
| Two lambda types | 7672 / 6272 B | 5980 / 6272 B |
| Four lambda types | 11852 / 7296 B | 9948 / 7296 B |
| Four sites, one named type | 5952 / 5760 B | 4280 / 5786 B |

Data/bss remains 0/512 B in all these objects. The named visitor's state
carries a per-call bias; reusing its type does not remove the four operations.
This demonstrates specialization cost and reuse, without promising that any
arbitrary visitor body has the same size.

In O2/Os the new known-position u32 read/write bodies equal direct-call bytes.
The legacy bodies have the same lengths for this same-owner fixture. New
Field/Command/Service entry size/alignment is 28/4, 20/4 and 24/4 B; legacy
Field is 96/32 B and legacy Command is 20/4 B. Scalar native read is 16 B;
write is 16 B at O2 and 12 B at Os. These are codegen facts, not cycle results.

Fresh legacy regression and optional protocol runs on NUCLEO-H7S3L8 on
2026-10-03 passed 3328 and 4300 checks per O2/Os image, respectively, and
restored the same 64 KiB Flash SHA. Their receipts name the prepublication
HEAD plus dirty state and captured inputs. They do not run this 97-check
consumer or establish native visitor timing. Full new MCU consumer/call-chain
qualification and cycle distributions remain Stage 14.
