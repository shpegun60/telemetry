# Typed traversal and native As access

Authors: Ruslan Kovtun (shpegun60), codexAi. MIT; repository license applies.

This correction slice precedes the Stage 12 client. It adds the same
`get/forEach/visit/empty/begin/end/operator[]` API to Field, Command and Service
tables/catalogs. Fields also gain explicit static/runtime `readAs/writeAs`.
The [public contract and examples](../../../lib/telemetry/README.md#native-api)
separate typed definitions, homogeneous erased iteration, native runtime access
and the existing encoded boundary. The final API uses
`<telemetry/Telemetry.hpp>` and namespace `telemetry`; protocol/session state
lives in an explicitly
selected [example](../../../examples/structured_protocol/README.md).

## Reproduce

```sh
python3 tests/structured/traversal/run.py --cxx g++ --build-dir build/traversal
python3 tests/structured/traversal/run.py --cxx g++ --null-checks --build-dir build/traversal-null
python3 tests/structured/traversal/run.py --cxx clang++-18 --sanitize --build-dir build/traversal-san
python3 tests/structured/traversal/run.py --arm --cxx arm-none-eabi-g++ --build-dir build/traversal-arm
```

The runner counts successful commands and intended compile/link rejections.
It does not label those command totals as runtime assertion counts. Current
host runs have 9 successful commands and 64 compile-fail controls; ARM has
65 successful commands and the same 64 controls (35 commands with
`--null-checks`, which omits the native byte-equality comparison). Logs, summaries, extracted
bytes, disassembly and `.su` files go to the caller-selected build directory.

- `Check.cpp`: exact references/types, constexpr traversal order, groups and
  empty tables, one selected runtime callback, invalid signed/wide IDs,
  move-only visitors and callback exception propagation. Traversal calls no
  live getter/service by itself. Numeric tests cover U64/S64 endpoints,
  truncation, negative fractions, overflow, NaN/Inf, negative zero, bool,
  unknown enum codes, structural mismatch and empty/rebound owner slots.
  The retained executable also runs after LTO/section GC; both new detail
  headers compile independently of an umbrella include.
- `Negative.cpp`: 36 rvalue borrowing/traversal controls across all six table
  families, invalid local/global typed positions, runtime ID provenance and
  explicit-template narrowing, volatile write input, static structural
  mismatch and 8 additional rvalue As controls. Diagnostics are checked by
  reason; accepting a negative case fails the runner.
- `NoHeap.cpp`: C++ allocation is replaced with termination. It traverses all
  categories including 4 KiB definitions, reads/converts a scalar, writes a
  scalar and explicitly requests a native Big value.
- `Arm.cpp`: direct/get/identity-As native code comparison and runtime frames
  in a model containing 4 KiB Field/Service types. Callback bodies are kept
  small. No Scalar/Getter/Setter object dispatch may appear in the assembly.
- `Scale.cpp`: 32/128-row baseline versus one used visitor specialization,
  reporting object `.text/.rodata/.data/.bss` rather than a claimed whole-image
  Flash delta. The baseline uses ordinary erased `index().find` metadata access.

## ARM evidence

CubeIDE GCC 14.3.1, Cortex-M7 hard-float, C++20, O2/Os/Og passed. In the
native comparison probe O2/Os direct, get and identity-As bodies are byte
identical: read 16 B, write 20 B, Command 28 B; Service 44 B at O2 / 36 B at Os.
Identical-function folding is disabled **only in that comparison translation
unit**, so tail aliases do not obscure bodies. Production flags are unchanged.
The unchanged Stage 08 direct/local/global byte-equality gates also pass with
normal compiler flags, and Field/Command/Service erased layouts remain
28/20/24 B with 4-byte alignment.

Adding runtime native reads made GCC Os outline the tiny Field read adapter
in the first probe, hiding a known owner address behind an optional return.
Inlining only that small adapter and its readAs bridge restored the direct
body; the complete probe object became 98 B smaller (4515 → 4417 B before
disabling identical-function folding). No encoded thunk/codec or Service
binding is forced inline. Debug/Og and original endpoint gates pass.

The exported runtime local/global visitor frames are 16 B at O2/Os/Og;
metadata `get_large_name` has a zero-byte frame. Those are individual frames,
not a maximum call-chain bound. A metadata visit does not construct Big.
Explicit `readAs<Big>` still returns an owning native optional<Big>; caller
and native return ABI costs are not replaced by Workspace. For bounded large
storage use the existing encoded methods with Workspace.

Incremental object sections for **one** visitor specialization, CubeIDE GCC:

| Rows | O2 text / rodata delta | Os text / rodata delta | Og text / rodata delta |
| --- | --- | --- | --- |
| 32 | +340 / +100 B | +296 / +102 B | +308 / +120 B |
| 128 | +1492 / +484 B | +1256 / +486 B | +1460 / +504 B |

Data/bss deltas are zero. Dispatch tables remain constant metadata. Code size
grows with rows and used visitor/value specializations, although ID selection
uses bounded array indexes rather than a scan. No extra fields are added to
table/entry objects. Unused methods/specializations do not add object storage.
These original traversal figures are object/codegen evidence, not hardware
cycle measurements. Later direct/visitor/encoded cycle and call-chain evidence
is retained in [Stage 14 qualification](../mcu/h7s/README.md).

## Preserved boundaries

Stage 08 typed/encoded checks, Stage 09 independent descriptor fixtures and
Stage 10 values/oracle checks remain enabled. The protocol example retains its
74 independently specified packet bytes, malformed-input checks, allocation
controls and four GC/LTO ABI mismatch controls. The two core-only/v3
qmake provider builds exclude example sources; `exchange/qmake.pro` selects
the example explicitly and exercises both handlers.

The protocol-relocation snapshot was measured on 2026-10-03: its
[historical protocol receipt](../exchange/h7s/receipt.json) records 4300 checks
per O2/Os image, original 64 KiB Flash restoration,
`source_head=cd8b636bc8a518fcc1a3021659f109281d47107e`, and `source_dirty=true`.
It does not establish visitor cycles for the final namespace migration. The
former 3328-check scalar receipt is historical and its runner is retired.
Current mixed/scale qualification is documented in the separate
[MCU suite](../mcu/README.md); captured manifests retain their own source
identity rather than being relabelled with later commits.
