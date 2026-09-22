# Current positional table checks

[C++20 resource checks](resources/README.md) validate the independent resource
core, packet protocol and external telemetry providers. They also compile/link
the application facade on Cortex-M7 without modifying telemetry descriptors.

[Flags and traversal](audit/FLAGS_TRAVERSAL.md) records the ABI 7 addition,
schema delta, lifetime/visitor checks and unchanged ARM instruction encodings
against `7751891`. Field remains 96 bytes on ARM32 and Command remains 20.
The [serializer follow-up](audit/SERIALIZATION_TRAVERSAL.md) adopts that traversal
inside JSON export and records its separate code-generation comparison.
[Minimal schema metadata](audit/SCHEMA_META.md) adds a format version and the
compile-time flag dictionary without changing values or descriptor rows.

[position_tables](position_tables/README.md) covers ABI 6, direct/local/global
ARM instruction comparisons, conversion parity and command-stride measurements.
The reports for ABI 5 and earlier below are retained historical measurements.
The [library audit](audit/README.md) records enum, binding-lifetime, noexcept
and compiler-mode repairs, regression coverage and the ARM comparison tool.

# Telemetry checks

The current RW32 Field uses a 32-byte alignment and 96-byte ARM32 stride,
with separate read and write cache lines. Compact Getter and Setter values are
8 bytes each on ARM32; `readType` is at offset 8. The
[RW32 H7S measurements](field_layout/h7s/RW32_RESULTS.md) record the production
choice; the [B32 report](field_layout/h7s/RESULTS.md) is its historical baseline.
ARM guards check size, hot member offsets and inlining of the finite-value check;
the historical 80-byte measurements later in this file describe earlier layouts.

The [JSON stack fixture](json_stack/README.md) records 588 real H7S measurements
per revision, including the newlib-nano formatter. The layered serializer's
largest observed stack writes are 1032 bytes at `-O2` and 976 at `-Os`; this
excludes caller-owned output buffers and is not a worst-case bound.

Run from the repository root with Python 3 and a GCC-compatible C++ compiler.
The checks have no Qt dependency. Generated binaries and logs go only to the
directory specified by `--build-dir`:

```sh
python3 tests/run_checks.py --cxx g++ --std c++17 --build-dir build/checks-gcc17
python3 tests/run_checks.py --cxx clang++ --std c++20 --build-dir build/checks-clang20
python3 tests/run_checks.py --cxx clang++ --std c++17 --sanitize --build-dir build/checks-sanitized
```

On Windows, use `python` and the installed Qt MinGW `g++.exe`. Include that
compiler's `bin` directory in PATH for its runtime DLLs. The corresponding
`telemetry_*_check.pro` files also build each suite through the library's `.pri`.

The runner executes fourteen suites (including native/dynamic table parity, all five slot types,
flags, Scalar visitation and indexed traversal), verifies
46 read/binding rejected programs, nine immutable-Field cases,
82 factory/command, 26 positional-table, 10 command-lifetime, 36 borrowed-field,
18 integer/enum-position, 18 owner-slot, 22 function-slot and 42 context/delegate-slot
rejection cases, 25 policy/traversal/visitor cases, plus two no-heap controls
(336 total; 343 in C++20 with structural
adapters and additional invalid position types), checks
each public header in isolation and checks that unsafe floating optimization
flags are rejected. It also checks nine cache-line configurations, five invalid
overrides and Field layout with explicit 64/128-byte alignment. It builds callers and
core, field-JSON and command-JSON static archives independently. Matching 64-byte layouts must link
and run; a 128-byte caller against each 64-byte archive must fail through its
ABI-tagged symbol. [TelemetryAbiLinkCheck.cpp](TelemetryAbiLinkCheck.cpp) proves
the core umbrella/anchor needs no JSON; [TelemetryJsonAbiLinkCheck.cpp](TelemetryJsonAbiLinkCheck.cpp)
checks the compiled serializer entry point. Frozen ABI 6 callers must also fail
against each ABI 8 archive. Two subprocess controls require invalid runtime
Persistent definitions (missing getter/setter) to terminate during construction.
Sanitized runs enable address, undefined-behavior and
float-cast-overflow checks, including stack use after scope/return, and stop
on the first diagnostic. Compiler warnings are errors. Each command's output
is retained in a separate log.

The preceding audit checkpoint `9f95e49` added six field-name checks (109 core, 584 total C++17
checks; 586 in C++20). `names_unique` now rejects null storage with nonzero
count and null names at every position, including singleton tables. Runtime
and constexpr cases cover empty tables/names, duplicates in separate storage,
and null names. Lookup/read/write implementations and the B32 layout are
unchanged at that checkpoint; its O2/Os `Probe.o` bytes matched the published `61443b0`
objects. RW32 changes those objects and adds a 676-transition descriptor
copy/move/assignment check, plus constexpr checks across different active types.
CI also runs the offline layout and stack evidence verifiers with
their mutation controls. Header/source, sanitizer, ARM and Qt checks remain.

The library migration notes now cover ABI revision 7, immutable Field definitions,
clean rebuilding of all translation units/static libraries with the same
cache-line configuration, raw-storage alignment, local-table stack cost and
structured bindings. Positional initialization and public metadata reads remain.
H7S measurements do not establish H753 firmware timing: a DWT run and task
stack measurement on the actual H753 integration remain target-specific work.

The [compact callback report](field_layout/h7s/COMPACT_CALLBACK_RESULTS.md)
retains an exact `a452283`/current board A/B: 1840 timing windows, independent
result checksums, image/object/source hashes and exact restoration of the
original 64 KiB image. Its receipt and CSV pass the offline verifier and nine
deliberately damaged-record controls.

JSON locale checks try a German numeric locale on Linux and Windows. Set
`TELEMETRY_TEST_LOCALE=de_DE.UTF-8` (Linux) or `German_Germany.1252` (Windows)
to require it: an unavailable requested locale fails the suite. Without that
variable, an unavailable locale is reported as a skipped check. GitHub Actions
installs and requires the locale. The test restores the previous locale.

The numeric oracle requires at least 64 bits of mantissa in `long double`
so it can represent all 64-bit integers exactly. It uses extended precision
and explicit `trunc` as an independent reference; the production code uses
source-precision comparisons before casting. Hosts without that precision
report the oracle as skipped; other suites still cover numeric endpoints.

## Cortex-M7 compile and link checks

```sh
python3 tests/run_arm_checks.py --build-dir build/arm
```

The ARM compiler and its sibling objdump must be on PATH. Supply `--cxx` or
`ARM_CXX` to select a CubeIDE `arm-none-eabi-g++` executable; `--objdump` can
override its sibling tool. The same script runs in GitHub Actions using the
Ubuntu 24.04 ARM GCC/newlib packages specified in the workflow. This CI
compiler is separate from the CubeIDE compiler used for local firmware work.

At both `-O2` and `-Os` it compiles 38 positive library/demo/test translation
units, including the codegen probes, with Cortex-M7 hard-float flags, no exceptions/RTTI
and warnings as errors. All sixteen codegen objects must have no startup
initialization and no writable data sections: their mutable owners are
deliberately external. The four exported IndexCodegen metadata symbols must
exist in `.rodata` with their expected sizes. BorrowedFieldCodegen pins its
exported 96-byte Field, while CommandTableCodegen pins the exported table view
and count. That probe also requires stack-free direct target branches from both
native dispatch levels and keeps the Scalar/indirect wrapper as a control.
OwnerSlotCodegen compares local/global reads, writes and commands with explicit
checked pointer calls, and separately requires direct-object and free-function
table calls to match ordinary calls without slot loads or presence checks.
FunctionSlotCodegen compares native local/global read, write, command and
converting command calls with handwritten function-pointer load/check/invoke
sequences. Its two-row Field table must remain 192 bytes in read-only storage.
LateBoundCodegen compares context/ref/owned slot operations with manual native
calls; its six-row table must remain 576 bytes in read-only storage. Runtime
checks cover capture ownership, move-only closures, replacement/destruction,
const views, mixed slot kinds, null contexts and schema/CRC stability.
TraversalCodegen pins flags at offset 20 without moving existing hot members,
and requires persistent native read/write paths to match unflagged controls.
Source static assertions also pin the ARM32 type layout. A minimal JSON consumer links with newlib-nano,
nosys stubs and enabled float formatting; it is not executed. The core ABI, field-JSON and
command-JSON objects are placed in separate static archives: normal 32-byte callers must
link, while forced 64-byte callers and frozen ABI 6 callers against each archive
must fail. The full traversal suite is compiled here; its 65536-element runtime
checks run on the host, not on a board.

Compiler versions, sections, symbols, disassembly and linker diagnostics are
retained under `--build-dir`; CI uploads those logs. This protects compilation,
constant storage and linking. Instruction-level performance still requires
inspection of disassembly; neither script proves board timing or stack peaks.

The structural refactor was also compared against exact checkpoint `036d8e8`
with the same local CubeIDE GCC 14.3.1 commands. All seven codegen probes at
both optimization levels were byte-identical (**14/14 objects**). JSON is not
part of that equivalence claim because its new string mode changes behavior;
the board fixture covers the resulting serializer separately.

## Signature factory and command codegen

The local CubeIDE GCC 14.3.1 build compares the seven existing probes against
`c6012d9` with identical Cortex-M7 flags at `-O2` and `-Os`: all 14 objects
are byte-identical, including constants and relocations. The
[receipt](factory-codegen.json) records compiler flags, source hashes and
object hashes. RW32 Field remains 96 bytes with 32-byte alignment on ARM32.

[FactoryCodegen.cpp](FactoryCodegen.cpp) additionally builds manual and
inferred definitions side by side. The comparison uses the same native
member/free functions, with external implementations to prevent whole-body
inlining. The runner requires inferred read wrappers to be no larger.

| Wrapper bytes, CubeIDE GCC 14.3.1 | Manual O2 | Inferred O2 | Manual Os | Inferred Os |
|---|---:|---:|---:|---:|
| Known member F32 read | 308 | 308 | 284 | 284 |
| Known free-function F32 read | 304 | 4 | 288 | 4 |
| Runtime Field Scalar read | 2388 | 2384 | 2210 | 2206 |

The free-function factory stores the native function pointer directly,
avoiding the manual `Getter::bind<&function>()` Scalar adapter; its read is
a direct tail branch. A manually supplied native function pointer already
has this advantage. The tiny size difference in runtime read does not prove
fewer executed instructions; compiler placement can affect instruction widths.
Member binding deliberately retains its existing implementation.

Additional inferred wrappers occupy 52/50 bytes for a known typed setter,
52/52 for a known two-argument command and 30/30 for runtime command dispatch
(`-O2`/`-Os`). Command stores no duplicated arity or parameter-type array and
occupies 24 bytes on ARM32. These are compiled wrapper sizes, excluding any
out-of-line callees; they are not cycle measurements. Command execution is
measured separately below.

The owning `CommandTable` now has a separate three-level probe with runtime
`float` and enum inputs. CubeIDE GCC 14.3.1 emits the following wrapper sizes at
both `-O2` and `-Os`:

| CommandTable path | Bytes | Wrapper stack | Target dispatch |
|---|---:|---:|---|
| `call<1>(float, Mode)` | 48 | 0 | Direct tail branch |
| `call(runtimeIndex, float, Mode)` | 60 | 0 | Typed branch, then direct tail branch |
| `index.execute(id, Scalar*, count)` | 52 | 4 | Descriptor-selected indirect tail branch |

The ARM runner parses disassembly and enforces these structural properties at
both optimization levels. Identical native argument types retain these paths.
Numeric and enum arguments of other types convert directly, without Scalar;
the runtime-position overload returns `ArgumentCountMismatch` when the selected
definition has another arity. The Scalar path remains for transport input.

Additional builds of FieldTableCodegen and CommandTableCodegen replace numeric
positions with scoped U64 enums and require identical instruction encodings at
both optimization levels. Eighteen invalid-position cases also compile on
ARM32 with required rejection diagnostics, including U64 values that would
wrap to a valid index if narrowed too early. A command probe compares native
`double, int` to `float, enum` conversion with a direct checked owner call:
local/global instruction streams must match and retain no Scalar, erased
dispatch or stack frame. Compared with the direct call, GCC 14 emits the same
stream; GCC 13 can invert a branch and reorder the success/error blocks, with
the same operations and constants. The gate permits this placement difference
but rejects extra operations. Host checks exercise all numeric pairs, enum inputs, target bounds,
borrowed/free/member callbacks and failure before owner side effects.

`CommandDispatchScalingCodegen.cpp` expands tables of 10, 32 and 100 commands,
where respectively 1, 2 and 7 definitions have the supplied two-argument arity
`(float, Mode)` and all others take one argument. The ARM gate
requires one range comparison plus exactly 1, 2 or 7 index comparisons, only
the matching concrete targets, no Scalar/indirect dispatch and no stack frame.
CubeIDE GCC 14.3.1 produced 72/72, 112/112 and 324/332-byte wrappers at
`-O2`/`-Os`; growth follows matching definitions rather than total rows.

`IndexCodegen.cpp` also compares compile-time-ID field access with the same
known `Field`. Static float/U16/read-only writes have identical normalized
instructions to direct `Field::write`. The gate requires static reads to match
the direct `Field::read` stream or improve it by eliminating its getter call
and stack work; erased dispatch is forbidden and converted reads must retain
the required numeric conversion. CubeIDE GCC 14.3.1 takes the latter path and
inlines the getter for both native and converted reads.

The dedicated [H7S DWT run](command_dispatch/h7s/README.md) measured the exact
source commit over nine 65,536-call windows per path and optimization level:

| Path | `-O2` cycles/call | `-Os` cycles/call |
|---|---:|---:|
| Compile-time index | 28.001 | 24.001 |
| Runtime index, native arguments | 29.001 | 27.001 |
| Runtime ID, prebuilt Scalars | 88.001 | 92.001 |

All checksums passed, all 54 timing windows are retained, and the original
64 KiB firmware image was restored and verified byte for byte.

Three live NUCLEO-H7S3L8 sessions then compared the exact compact baseline with
three Command candidates at both `-O2` and `-Os` (95 checked windows per image,
380 per session). Full-line and half-line layouts grew ARM rows from 24 to 32
bytes. At the production `-O2`, repeated execution was unchanged and the
1024-row sequential/random cases were up to 0.38% slower; a reordered compact
row was 4.35% slower. Half-line alignment improved `-Os` execution by
7.7-8.0%, but made standalone lookup 9.1% slower. Since the speed-oriented
firmware build uses `-O2`, production retains the original 24-byte layout.
Every session validated all checksums and restored the original 64 KiB image;
the before/after SHA-256 was
`a5903024dba85fab5121150ca8ad13482f97384aa450aab67413881991fb9456`.
A fourth final control used the retained compact layout and matched the baseline
cycle for cycle in every reported profile at both optimization levels.

## Audit checkpoint, 2026-09-19

Reviewed all active library files, the bundled delegate interfaces used by
Getter/Setter, the demo's owned lifetimes, read/write normalization, direct
lookup, public documentation and tests. Confirmed fixes:

- Null JSON output with positive capacity previously reached `snprintf` and
  reproduced an invalid write under AddressSanitizer. It now returns zero.
- A decimal-comma locale previously serialized 1.5 as `[1,5]`. The serializer
  now writes a decimal point without changing the application's locale.
- F32 value `nextafter(1.0f, 2.0f)` previously became JSON `1`. Nine significant
  digits preserve the original value. F64 retains seventeen digits.
- An exhausted output buffer previously continued calling getters. It now
  stops immediately, and every positive-size output stays NUL-terminated.
- CubeIDE's `newlib-nano/newlib.h` disables `_WANT_IO_LONG_LONG`. U64/S64 now
  use bounded decimal conversion independent of that printf feature, with
  unsigned arithmetic for the magnitude of INT64_MIN.

GCC 13.1 and Clang 18 passed 411 C++17 and 413 C++20 runtime checks; Clang
C++17 also passed with all three sanitizers. This includes 121 conversion
pairs, each with endpoints and 1024 samples, and JSON round trips with 4096
samples plus endpoints for each of four number types. Both compilers rejected
the invalid programs for the expected reasons. No findings remained in the
reviewed numeric conversion and direct-index implementations.

CubeIDE GCC 14.3.1 compiled the library, demo, tests and all five code-generation
probes for Cortex-M7 at `-O2`/`-Os` without exceptions or RTTI. The known matching
typed reads still branch directly to getters, and F32-to-U16 still checks two
source-precision bounds before one conversion. A minimal JSON consumer linked
with `nano.specs`, `nosys.specs` and `-Wl,-u,_printf_float`; the supplied nosys
system-call stubs generated their expected linker warnings. Neither that
consumer nor firmware was executed on a board during this audit. Its source is
[EmbeddedLinkCheck.cpp](EmbeddedLinkCheck.cpp), with linking flags in its header.

## Enum metadata checkpoint, 2026-09-19

This records the enum-only checkpoint `ae1d2dd`. The subsequent write-limit
checkpoint below changes write acceptance and the metadata layout.

The sixth suite adds 45 checks: all numeric underlying widths, numeric-only
read/write behavior even for unlisted codes, checked conversion, exact U64/S64
dictionary keys, automatic and explicit names, metadata copying/defaults,
early description cancellation, fingerprint changes and escaped custom names.
It sweeps every output-buffer size through complete enum schemas, including
lengths inside escaped strings. Compile rejection cases also cover non-enums,
unnamed values, duplicate codes, empty scans and direct enum read/write inputs.

GCC 13.1 and Clang 18 passed all six suites (456 C++17 / 458 C++20 checks) and
all nineteen expected rejections. Clang C++17 passed ASan, UBSan and
float-cast-overflow. The enum suite also passed its standalone qmake project.
Qt 6.10.1 Release passed offscreen startup; separately parsed demo JSON contains
20 fields and the Mode dictionary while retaining numeric values.

CubeIDE GCC 14.3.1 compiled all six probes, six suites, the minimal consumer,
library and demo for Cortex-M7 at `-O2`/`-Os`. The minimal consumer now includes
enum schema generation and links with newlib-nano. `EnumCodegen.cpp` shows
matching numeric instructions for enum/plain U16 fields, aside from table
addresses/offsets. Known typed reads branch to the same getter; dynamic reads
and writes never load the schema callback at Field offset 16. Numeric tags
remain at offset 12. Field grows from 36 to 40 bytes; direct lookup keeps its
14-instruction successful path (13 for the fixed view). These are object-code
observations, not board timing measurements.

The enum tests also capture a reflection prerequisite on Clang: an enum
nested in a class template needs its enumerator list instantiated before an
automatic scan. Using a named enumerator first, or listing explicit values
in `enumType`, does that. Default scan limits and this prerequisite are
documented in the library and dependency READMEs.

## Write-limit checkpoint, 2026-09-19

This records checkpoint `1893e61`; the default-first API and compact schema
checkpoint below supersede its argument order and native-bound JSON text.

The seventh suite adds 66 checks and constexpr assertions for native extrema,
custom bounds, defaults, enum-derived limits, full-width 64-bit comparisons,
inclusive endpoints, conversion before range checks and required schema
properties. Reads ignore the interval while retaining declared-type conversion.
Defaults do not initialize owners or replace unavailable readings. Eight new
compile rejection cases cover inconsistent, non-finite and unrepresentable
definitions. The independent numeric oracle now separately checks finite
write acceptance while retaining NaN/Inf conversion and read coverage.
F32 schema extrema and defaults round-trip through a double JSON reader and
remain writable. Metadata uses 17 significant digits for this contract;
the former nine-digit maximum could exceed FLT_MAX when parsed as double.

GCC 13.1 and Clang 18 passed 522 C++17 / 524 C++20 checks and all 27 expected
rejections. Clang C++17 also passed ASan, UBSan and float-cast-overflow. The
limits suite passed its qmake build; the Qt Release application passed
offscreen startup. Parsed demo JSON contains all three required properties
for every one of its 20 fields. Mode has bounds 0..2 and default 1;
VoltageLimit has bounds 1..1000 and default 250.

CubeIDE GCC 14.3.1 compiled the library, demo, seven suites, seven codegen
probes and minimal JSON consumer at `-O2`/`-Os`, without exceptions or RTTI.
The minimal consumer linked with newlib-nano. Native limits use one variant
holding a triple, keeping ARM32 FieldType at 40 bytes and Field at 80 bytes;
three separate Scalars would require a 96-byte Field. Numeric tags are at
Field offset 16, schema callbacks at 20. Reads do not load limits or callbacks.

The new limits probe shows direct getter branches for bounded reads, no
interval comparison for a full-range U16 write, and native F32 comparisons
for restricted float writes. A 10..20 U16 interval folds to subtraction and
one unsigned comparison. A known rejected write is a constant return without
callback dispatch. Enum/plain fields with equal limits have equal numeric
operations, with table-address and instruction-encoding differences. These
are compiled-object results, not measured board cycles or cache behavior.

## Default-first factory and compact bounds, 2026-09-19

This records checkpoint `28cc59c`; the follow-up below fixes an explicit-type
binding issue missed by the earlier lifetime checks.

`numericType<T>` now takes default, minimum, maximum. Zero arguments keep
the native range and zero/false default; one sets only the default; two set
default/minimum and retain the native maximum. Compile-time assertions cover
all native alternatives, and three new rejected programs cover default
overflow, NaN and a default below a supplied minimum.

Ordinary numeric fields serialize each native bound as null, resolved from
the schema's `t`. Custom endpoints remain exact numbers. Enum and Bool
bounds always stay explicit, including enum extrema equal to native limits.
Defaults are always explicit. New checks cover all eleven alternatives,
partial ranges, adjacent U64/S64 endpoints, F32/F64 limits resolved or parsed
as double and written back, and metadata under a decimal-comma locale.
The format marker changes schema fingerprints so cached old schemas refresh.

Reviewed every active library source/header, the used delegate bindings,
demo lifetimes, numeric conversions, lookup, serialization and build files.
Runtime metadata construction exposed GCC 13's maybe-uninitialized diagnostic
on intermediate optional results. Normalizing local Scalars before extracting
native values removes those intermediates and the diagnostic without disabling
warnings; constant evaluation and the public conversion policy are preserved.
No additional library defects were found within the caller contracts below.

GCC 13.1 and Clang 18 passed 564 C++17 / 566 C++20 checks and all 30 expected
compile failures, standalone headers and rejected floating optimization flags.
Clang C++17 also passed ASan, UBSan and float-cast-overflow. Qt 6.10.1 Release
passed offscreen startup. The parsed demo schema has 20 fields and is 2002
UTF-8 bytes, down from 2359 with the previous serializer. Mode retains
0..2/default 1, Bool retains false..true/default false, and value JSON still
contains exact U64/S64 digits.

CubeIDE GCC 14.3.1 compiled 17 sources (library, demo, seven suites, seven
probes and the minimal consumer) at both `-O2`/`-Os`, without exceptions/RTTI
and with warnings as errors. The minimal consumer linked with newlib-nano.
In `LimitsCodegen.cpp`, ScalarType, zero-argument numericType and default-only
numericType produce identical numeric write operations for U16 and F32,
apart from addresses/branch encodings. U16 has no range comparison; F32
retains only its native finite-value checks. Bounded reads still branch
directly to getters. No read/write path consults enum metadata. These are
compiled-object observations; no board execution or cycle timing was done.

## Final lifetime and integration follow-up, 2026-09-19

Rechecked active library code, tests, dependency bindings, demo ownership,
documentation and CI. A supplied external review was checked against source
and reproductions, rather than taken as verification by itself.

The confirmed defect was in both method-binding wrappers. Explicit
`bind<&Owner::method, const Owner>(Owner{})` and the `const Owner&` form could
borrow a temporary, even though ordinary deduced rvalues were rejected.
Clang AddressSanitizer reproduced stack-use-after-return for all four
getter/setter combinations on `28cc59c`. The wrappers now reject reference
template types and delete the rvalue overload. Four new compile-fail cases
cover those bypasses; another covers ordinary setter rvalues. Positive checks
cover live explicit/const owners, constexpr binding, multiple-inheritance
member adjustment and preservation of existing bindings when a function-pointer
conversion throws. Caller-owned lifetime and synchronization remain required.

Schema and value serialization now escape catalog names, field names and units
using the existing bounded string writer. They accept UTF-8, quotes, backslashes
and control bytes. Tests cover every non-NUL control byte, empty names/units,
all output capacities around escaped text and stopping before getter calls.
`catalog_names_unique` adds optional constexpr duplicate-name validation,
without adding checks to lookup/read/write. The enum-default comment now
matches the existing acceptance of unnamed numeric codes between extrema.
The numeric oracle also verifies that failed in-place conversion preserves
the exact original value and tag, including signed zero and NaN handling.

GCC 13.1 and Clang 18 passed 578 C++17 / 580 C++20 checks and all 35 expected
compile failures. Clang C++17 also passed ASan, UBSan and float-cast-overflow,
with stack-use-after-scope/return detection enabled. All runs required the
decimal-comma locale; standalone headers and floating-optimization rejections
passed. Qt 6.10.1 Release passed offscreen startup with warnings as errors.

The new ARM runner passed on CubeIDE GCC 14.3.1 and Ubuntu ARM GCC 13.2.1 at
both optimization levels: 17 translation units, seven storage/codegen probes
and the newlib-nano consumer link. The storage guards also rejected separately
compiled objects containing a startup constructor, writable data or a changed
table size. CI now runs that same ARM check. All 14 CubeIDE codegen objects
are byte-identical to a fresh build of the exact `28cc59c` sources, so
these fixes add no instructions to the measured numeric/read/write/index paths.
Field remains 80 bytes on ARM32; external shared schema storage was considered
but would require a separate API/layout decision. No remaining correctness
defect was found within the caller contracts below; this is evidence from the
listed checks, not a proof for arbitrary callbacks or application lifetimes.

## Compact callbacks and complete factory API, 2026-09-20

The publication candidate replaces Getter's previous 12-byte ARM variant with
an 8-byte exact payload/invoker pair; Setter uses the same representation.
Direct parameter-form getter/setter pairs retain their native numeric function
pointer types. Template member/free pairs retain enum identity and generated
owner adapters. Commands additionally borrow named stable callable lvalues;
temporary, generic, overloaded and throwing forms are compile-time errors.
Fields now also borrow capturing lambdas and stateful functors from named stable
lvalues while capture-free lambdas stay on the native function-pointer path.

Indexed `arg<N>` command metadata can be partial and arbitrarily ordered;
signature positions without metadata are inferred. `CommandTable{...}` and
the global `CommandCatalogTable{group(...)}` borrows local tables; CommandTable owns metadata and expose ordinary Command views.
They require direct C++17 construction and are non-copyable/non-movable because
their descriptors point into their own storage. Pointer, reference and index
views can only be extracted from lvalue tables, so a temporary table cannot
produce a dangling view. `CommandCatalogTable` intentionally exposes no flat
`CommandIndex`; packed IDs are resolved through `CommandCatalogIndex`, including
a constexpr owning group-1 regression. The older positional `commandArgs` API
remains source-compatible.

Explicit `enumSpec<values...>()` covers sparse/subset dictionaries for both
fields and command parameters. `CommandCatalogIndex` adds packed group/index
lookup and grouped schema paths without growing the 24-byte ARM Command.
ABI revision 6 covers public descriptor/index offsets and the private nested
Scalar, Getter, Setter and FieldType layout. At that checkpoint the compact
core stopped depending on tiny_delegate. The later delegate slot family uses
the bundled v1.2.0 header without changing the compact Getter/Setter core.

Local MinGW GCC 13.1 passed 729 C++17 and 732 C++20 runtime checks, all 39
existing invalid programs, nine immutable-Field cases and 82 factory/command
invalid programs. Standalone headers, floating-mode rejection, nine positive
and five invalid cache-line configurations, explicit 64/128-byte layouts and
matching/mixed core/field-JSON/command-JSON ABI archives all passed. Qt 6.10.1
Release built and completed the grouped-command offscreen smoke test.

MSVC 19.50 separately built the three library translation units and passed
608 applicable C++17 checks and 611 applicable C++20 checks with
`/permissive- /W3 /WX`. Its numeric oracle intentionally skips because this
implementation gives `long double` only 53 mantissa bits; that is not reported
as numeric-oracle coverage. All 130 invalid C++17 programs were rejected.

CubeIDE GCC 14.3.1 compiled 28 sources and eleven read-only probes at both
`-O2` and `-Os`, linked the newlib-nano consumer and all three independent ABI
archives, and rejected every mixed layout. ARM32 sizes are Scalar 16, Getter 8,
Setter 8, FieldType 48, Field 96/aligned 32 and Command 24 bytes.
The static field and command-scaling probes additionally enforce the new
compile-time access and signature-filtered dispatch properties described above.

The [exact board A/B](field_layout/h7s/COMPACT_CALLBACK_RESULTS.md) covers
1840 windows and restored the original image byte for byte. A fresh build of
the publication candidate reproduced measured Current Probe/Benchmark objects,
ELF and BIN at both optimization levels.

## Contracts the caller supplies

Passing invalid borrowed storage cannot be made safe by an index lookup.
Owners, arrays and strings must outlive their readers/writers, explicit counts
must describe actual array extents, and bound objects must keep their address.
Metadata stays immutable during use. The owner provides synchronization and
any coherent snapshot across fields. JSON metadata follows the documented
non-null UTF-8 string and uniqueness restrictions, output does not overlap
inputs, and the application does not change the process locale concurrently.

These checks establish behavior for those contracts on the tested toolchains.
They do not prove correctness of arbitrary application callbacks or concurrent
access, nor measure latency or total stack use on the device.
