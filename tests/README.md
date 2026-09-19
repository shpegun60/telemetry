# Telemetry checks

The current Field layout uses a 32-byte alignment and 96-byte ARM32 stride.
The [layout experiment and H7S measurements](field_layout/h7s/RESULTS.md)
record the production choice. ARM guards check its size and hot member offsets;
the historical 80-byte measurements later in this file describe earlier layouts.

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

The runner executes seven suites, verifies thirty-five rejected programs, checks
each public header in isolation and checks that unsafe floating optimization
flags are rejected. Sanitized runs enable address, undefined-behavior and
float-cast-overflow checks, including stack use after scope/return, and stop
on the first diagnostic. Compiler warnings are errors. Each command's output
is retained in a separate log.

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

At both `-O2` and `-Os` it compiles all positive test sources, the library,
demo and codegen probes, with Cortex-M7 hard-float flags, no exceptions/RTTI
and warnings as errors. All seven codegen objects must have no startup
initialization and no writable data sections: their mutable owners are
deliberately external. The four exported IndexCodegen metadata symbols must
exist in `.rodata` with their expected sizes. Source static assertions also
pin the ARM32 type layout. A minimal JSON consumer links with newlib-nano,
nosys stubs and enabled float formatting; it is not executed.

Compiler versions, sections, symbols, disassembly and linker diagnostics are
retained under `--build-dir`; CI uploads those logs. This protects compilation,
constant storage and linking. Instruction-level performance still requires
inspection of disassembly; neither script proves board timing or stack peaks.

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
