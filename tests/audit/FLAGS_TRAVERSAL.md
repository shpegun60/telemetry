# Field flags, visitors and indexed traversal

Baseline: `77518915933a2997871b2916637efe4b4ce5d0b5`, ABI 6.
Its [exact-SHA CI run](https://github.com/shpegun60/telemetry/actions/runs/35522976971)
passed before this change. This addition uses ABI 7. Persistence storage is
outside its scope.

## Contract

- `FieldFlag::Persistent` is bit 0 of a 32-bit `FieldFlags` mask; default is zero.
  The constructor accepts a named flag, `operator|` combines masks, and
  `fromRaw()` deliberately preserves unknown bits. Bare integers are rejected.
- `field(...).withFlags(...)` replaces policy while retaining the same native
  definition type. Persistent requires both descriptor capabilities. A missing
  callback rejects constant evaluation or aborts runtime construction; an empty
  declared slot remains a valid, currently unavailable source/target.
- Field schema always exports `f`, independently of `w`. All four bytes enter
  the fingerprint in little-endian order. Values and command schema do not change.
- `Scalar::visit()` handles all twelve alternatives on mutable/const lvalues;
  temporaries are rejected. Visitor exceptions propagate when enabled.
- `catalogs()` exposes computed group/entry IDs through borrowed views. Empty
  groups and reserved entries preserve their positions. `size_t` iteration
  permits a 65536 end position without narrowing it to zero.
- `Command::forEachParameter()` uses the existing synchronous descriptor callback,
  requires a non-throwing bool-convertible result and stops on false. No parameter
  array or visitor copy is created.

See the [public API contracts](../../lib/telemetry/README.md#field-policy-flags)
for examples and ownership details.

## Layout and code generation

| ARM32 property | Baseline | ABI 7 |
| --- | ---: | ---: |
| Field size / alignment | 96 / 32 | 96 / 32 |
| Getter offset | 0 | 0 |
| readType offset | 8 | 8 |
| name / unit offsets | 12 / 16 | 12 / 16 |
| policy offset | padding | 20 |
| Setter / FieldType offsets | 32 / 40 | 32 / 40 |
| Command size / alignment | 20 / 4 | 20 / 4 |

On the tested x64 host with 64-byte cache lines, Field remains 128 bytes and
policy uses offset 40. The exact ABI tuple includes policy offset, size and
alignment even though existing member offsets do not move. Each separately
built core, field-JSON and command-JSON archive rejects the frozen ABI 6 tuple.
As a positive control, those same frozen callers were linked successfully
against the archived baseline ARM libraries at both optimization levels.
Inline-only modules still need the documented explicit ABI anchor at a module
boundary; an unused mismatched header cannot be detected by a linker.

CubeIDE ARM GCC 14.3.1 rebuilt the archived source and the updated source with
identical Cortex-M7 hard-float options at `-O2` and `-Os`. All **30 existing
probe/optimization pairs have identical instruction encodings**, including
literal pools. The comparer ignores addresses, symbol labels and relocation-only
lines; this is evidence about these probes, not a claim that complete firmware
images are byte-identical. [Retained hashes](flags-codegen.json) record each pair.

The new `TraversalCodegen.cpp` independently requires persistent and unflagged
native reads/writes to produce the same instructions at local/global levels.
On GCC 13 `-Os`, one wrapper includes an unreachable alignment nop after its
terminal tail branch. That padding is allowed only by this comparison; an
executed nop or changed operation remains significant. Existing baseline
encoding comparisons retain every nop.

No board cycle or stack-peak measurements were made for this addition. The
unchanged old probe encodings and descriptor sizes establish the checked
regression boundary; they do not establish timing on every compiler/target.

## Verification

- All fourteen host suites passed with MinGW GCC 13.1 C++17 and Clang 18 C++20
  with address, undefined-behavior, float-cast-overflow and lifetime checks.
- New metadata/traversal suite: **83 checks**, including real arrays and groups
  of 65537 descriptors clipped to 65536, the final `UINT32_MAX` ID, every Scalar
  alternative, all five slot kinds, full 32-bit masks and early-stop visitors.
- **24 new rejected programs** cover invalid policy, implicit masks, borrowed
  lifetime, constness and visitor contracts. Two subprocess tests verify runtime
  construction aborts. MSVC 14.50 passed these rejections and both traversal/table
  suites under C++17 and C++20.
- Cortex-M7 GCC 14.3.1 and 13.2.1 passed 38 translation units and 16 constant-storage
  probes per optimization level, old/new ABI rejection, cache-line mismatch and
  newlib-nano linking. The hardware executable is linked only.
- Qt 6.10.1 MinGW release built with warnings as errors and passed its offscreen
  smoke test. The demo marks VoltageLimit and Mode Persistent.

The local Linux locale-dependent JSON subchecks reported an unavailable locale;
the MinGW locale checks passed. CI requires a generated German locale rather
than allowing that skip.

The ABI 6/7 schema comparison uses the same public declarations for both source
trees. The two-field fixture gains only `f:0` in each row and its field
fingerprint changes from `9dc45874` to `c623b5b4`. Values JSON and command schema
are byte-identical. A dedicated runtime suite separately checks nonzero masks
and each mask byte's contribution to the hash.

## Reproduce

Create an archived checkout of the baseline, keeping it independent of edits:

```sh
git archive 77518915933a2997871b2916637efe4b4ce5d0b5 -o baseline.tar
# Extract baseline.tar to a separate directory, then run from that directory:
python tests/run_arm_checks.py --cxx /path/to/arm-none-eabi-g++ --build-dir /path/to/before-arm
# Run the same compiler from the current repository:
python tests/run_arm_checks.py --cxx /path/to/arm-none-eabi-g++ --build-dir /path/to/after-arm
python tests/audit/compare_arm_probes.py --before /path/to/before-arm --after /path/to/after-arm --allow-new-probes --output /path/to/encodings.json
python tests/audit/compare_flags_schema.py --baseline /path/to/baseline --cxx g++ --output /path/to/schema-comparison
python tests/run_checks.py --cxx g++ --std c++17 --build-dir /path/to/host-checks
```

`--allow-new-probes` permits added probes but still requires and compares every
old one. Generated binaries/logs belong in the selected output directories;
the baseline and updated compiler/options must match for the A/B comparison.
