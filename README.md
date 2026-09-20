# Telemetry playground

A Qt Widgets application for exercising the standalone C++17 telemetry
library copied from the analyzer. All values in this application are
simulated; it does not connect to an instrument.

Repository: [shpegun60/telemetry](https://github.com/shpegun60/telemetry).
Clone the complete project, then open `telemetry.pro` in Qt Creator:

```sh
git clone https://github.com/shpegun60/telemetry.git
```

The library, its magic_enum dependency, demo and checks are all included in
this repository. The delegate tree is an optional synchronized companion for
applications that use it separately; telemetry itself does not depend on it.
No analyzer firmware checkout or Git submodule is required.

Licensed under the [MIT License](LICENSE), with the same license text and
copyright notice as the delegate project. The reusable telemetry library
carries its own copy of [LICENSE](lib/telemetry/LICENSE); the bundled delegate
retains its upstream [MIT license](lib/delegate/LICENSE), as does
[magic_enum](lib/magic_enum/LICENSE).

```text
telemetry.pro                       Qt Creator entry point
app/
  main.cpp, mainwindow.*            Qt application and value display
  demo/DemoCatalog.*               simulated sources and field tables
lib/telemetry/
  Telemetry.h                       public umbrella (no forwarding headers)
  core/, field/, catalog/           public numeric, field and lookup layers
  command/                          inferred signatures, owning tables and direct lookup
  abi/                              independent layout guard and link anchor
  serialization/                    optional JSON public API and implementation
  detail/                           private storage/conversion/JSON helpers
  telemetry.pri                     reusable qmake include
lib/delegate/
  delegate.pri, tiny_delegate.hpp  optional upstream v1.2.0 companion library
  LICENSE, README.md               license and source revision
lib/magic_enum/
  magic_enum.hpp, magic_enum.pri   pinned v0.9.8 for optional enum schema metadata
  LICENSE, README.md               upstream license and source revision
tests/                             standalone checks without Qt
build/                            generated files, ignored by git
```

Open `telemetry.pro` in Qt Creator and build with the installed Qt 6 MinGW
kit. The project includes `lib/telemetry/telemetry.pri`; no separate library
build step is required. Its `.pri` includes the pinned magic_enum header.
JSON is enabled by default. Add `CONFIG += telemetry_no_json` before including
the `.pri` when a consumer needs only catalogs, lookup and the ABI anchor.
Select `telemetry_playground` as the run target if
Qt Creator still remembers the original blank application's target.

## Git synchronization in Qt Creator

The repository root is the directory containing `telemetry.pro`. The local
`main` branch tracks `origin/main` at the GitHub URL above.

- Save edited files, then use **Tools > Git > Local Repository > Commit**
  to select changes and create a local commit.
- Use **Tools > Git > Remote Repository > Push** to publish those commits.
- Use **Tools > Git > Remote Repository > Pull** to receive committed changes
  from another computer.

See the [Qt Creator Git documentation](https://doc.qt.io/qtcreator/creator-vcs-git.html).
If the project was already open when Git was initialized, reopen the project
to refresh repository detection. The equivalent terminal workflow, from this
directory, is:

```sh
git status
git pull --ff-only
git add <edited-files>
git commit -m "Describe the change"
git push
```

Build output and personal Qt Creator kit/run settings are ignored by Git.
Choose an installed Qt kit after cloning on another computer. The bundled
vendored headers and licenses retain their original bytes across checkouts.

## Playground examples

The table displays sixteen fields and refreshes every 500 ms. Schema/value
JSON and two simulated commands appear below it. Reset clears the simulated
counter; Configure applies a voltage limit and enum mode together. The command
schema is grouped under the same `meter` path as its fields and is generated
from the C++ signatures. `--smoke-test` exercises both
commands and closes after 1.2 seconds; it also works with `-platform offscreen`.

[DemoCatalog.cpp](app/demo/DemoCatalog.cpp) now uses `makeField` throughout:
getter return types define numeric/enum metadata, while typed setters receive
native values. Inline capture-free lambdas and free/static functions are also
supported; see [factories and commands](lib/telemetry/README.md#signature-inferred-fields-and-commands).
Its commands use a directly constructed owning `CommandCatalogTable`; sparse
`arg<N>` metadata is reordered by signature index at compile time and omitted
arguments receive inferred metadata.
The constructor binds fields to its owned sensor. Its arrays and owner stay
at stable addresses, so DemoCatalog is neither moved nor copied.

IDs pack a 16-bit group and a 16-bit field position. Meter is group 0
(IDs `0..5`); sensor is group 1 (IDs `65536..65537`); integer examples are
group 2 (IDs `131072..131079`). The integer rows show unsigned maxima and
signed minima for every 8/16/32/64-bit type. Their display reads the simulated
sources directly, keeping all U64/S64 digits without conversion to double.
The sources stay stable while the table and JSON are refreshed.
The `Mode` row supplies only `Mode::Auto`; the signature identifies `Mode` and
its named values are discovered at compile time. Its value remains U16 while
the schema JSON adds `"enum":{"0":"Off","1":"Auto","2":"Manual"}`. Its write interval
is 0..2 and default is Auto (1). No dictionary check runs during lookup, read
or write. Explicit `enumSpec<...>` remains available for sparse, large or
intentionally filtered dictionaries. See the [enum contract and large-code
examples](lib/telemetry/README.md#enum-dictionaries-for-schemas).
Each field row starts
with `makeId(group, position)`. `DemoCatalog::find(id)` uses one direct
group lookup and one direct field lookup, with a bound check at each level.
For example:

```cpp
if (auto temperature = demo.index().read<double>(telemetry::makeId(1, 0))) {
    // *temperature is a plain double.
}
auto value = demo.index().read(telemetry::makeId(1, 0)); // Scalar, value.type() == F64.
```

The schema publishes group IDs, packed field IDs, local positions `i` and
setter presence `w`, and required `min`, `max`, `default` properties.
Native endpoints of ordinary numeric fields use `null`: resolve them from `t`.
Custom bounds and defaults remain explicit. Enum and Bool bounds are always
explicit, even at the native endpoints of their underlying type.
The default JSON mode preserves numeric U64/S64 output. For JavaScript clients,
`JsonOptions{JsonInt64Mode::String}` quotes only U64/S64 values and their
non-null schema bounds/defaults; U32/S32 and all other alternatives retain
their JSON types. This wire choice does not change `schemaCrc()`.
VoltageLimit (ID 4) and Mode (ID 5) are writable; other fields are read-only.
VoltageLimit declares write limits 1..1000 and default 250:

```cpp
const auto result = demo.index().write(telemetry::makeId(0, 4), 275);
// Applied; an ordinary int is converted to the field's F32 before its setter.
```

`numericType<float>(250, 1, 1000)` defines those limits at compile time, with
arguments in **default, min, max** order. `numericType<float>(250)` keeps the
native extrema; `numericType<float>()` also keeps default zero and is equivalent
to `ScalarType::F32`.
`enumType<Mode>(Mode::Auto)` derives enum extrema automatically and sets a
chosen default. Defaults are metadata, never automatic writes. Only writes
check the inclusive interval, after conversion. Reads ignore the interval.
See [write limits and defaults](lib/telemetry/README.md#write-limits-and-defaults).

Sensor methods return plain double/bool, and Meter methods use plain float;
source getters do not need to know Scalar. The catalog owns the adaptation.
`declaredType` controls both directions: a getter returning double can back
an F32 field, and its result is checked and converted before publication.
`read<T>()` also respects the declared type before converting to T; reading
a U16 field with a getter returning 12.75 as double therefore gives 12.0.
Scalar stores its value and tag together in a private `std::variant`.
Factories and native return values remain supported; exact manual access
uses `get<T>()` or the non-throwing `getIf<T>()`.

For a namespace-scope constexpr catalog array, `CatalogIndex::bind<catalogs>()`
also enables `read<makeId(group, field)>()`, whose optional native result type
is inferred at compilation. DemoCatalog's instance-bound sensor uses ordinary
`read<T>(id)`. See [read examples](lib/telemetry/README.md#scalar-typed-and-inferred-reads).

Groups and fields are densely numbered from zero. A wrong field ID clips
only that group's visible prefix; a wrong group ID clips the whole group
list. Requests outside those prefixes return null. JSON uses the same
accepted view. See [lookup contracts](lib/telemetry/README.md#packed-ids-and-direct-lookup).
The previous range/pointer-slot implementation is retained in
[archive/id_ranges](archive/id_ranges/README.md), outside the active build.

Build the independent checks by opening
[telemetry_check.pro](tests/telemetry_check.pro),
[telemetry_write_check.pro](tests/telemetry_write_check.pro),
[telemetry_read_check.pro](tests/telemetry_read_check.pro),
[telemetry_json_check.pro](tests/telemetry_json_check.pro),
[telemetry_numeric_check.pro](tests/telemetry_numeric_check.pro),
[telemetry_enum_check.pro](tests/telemetry_enum_check.pro) and
[telemetry_limits_check.pro](tests/telemetry_limits_check.pro) with the same
kit, or run qmake and mingw32-make from a separate build directory:

```powershell
# Run in the Qt kit's command environment, from this directory.
New-Item -ItemType Directory -Force build/checks | Out-Null
Push-Location build/checks
qmake ../../tests/telemetry_check.pro CONFIG+=release
mingw32-make -j4
./telemetry_check.exe
Pop-Location
```

The checks use the same `.pri` as the application. A [Qt-independent test
runner](tests/README.md) also builds the suites, checks expected compilation
failures and can enable sanitizers. GitHub Actions runs GCC/Clang C++17/C++20,
Clang sanitizers, Cortex-M7 compile/storage/link checks and an offscreen Qt application check.
Library integration and contracts are in [lib/telemetry/README.md](lib/telemetry/README.md).

Verification after closing the field/command core, 2026-09-20:

- Qt 6.10.1 / MinGW 13.1: Release application build and offscreen grouped-command smoke test.
  The same MinGW compiler passed 109/109 core, 109/109 write/getter/setter,
  87/87 read, 33/33 JSON, 121/121 numeric oracle, 45/45 enum, 108/108
  limits, 40/40 factory and 67/67 command checks with C++17 (**719 total**).
  C++20 also covers char8_t (111/111 write and 88/88 read; **722 total**).
  Thirty-five expected compilation failures cover invalid bindings/reads,
  temporary arrays, invalid Scalar access, enum contracts and invalid limit definitions.
  Nine additional programs reject mutation/assignment of immutable Field
  definitions; 76 more reject invalid factory and command definitions,
  including temporary captured closures, invalid indexed command metadata and
  views extracted from temporary owning command tables.
  Cache-line defaults and explicit overrides have positive and
  negative compilation checks. Standalone public headers compile; fast-math and finite-math-only builds
  are rejected. The Qt table also
  showed the exact boundary values for all eight integer types, matching
  the raw value JSON, including `UINT64_MAX` and `INT64_MIN`.
- MSVC 19.50 built the three library translation units and passed all eight
  applicable suites under `/std:c++17` and `/std:c++20` with `/permissive-`
  and warnings as errors: 598 and 601 checks respectively. Its independent
  numeric oracle reports an explicit skip because MSVC `long double` has only
  53 mantissa bits. All 120 invalid C++17 programs were still rejected.
- CI runs all nine suites on GCC/Clang C++17/C++20. Clang 18 C++17 additionally
  enables ASan/UBSan and float-cast-overflow checks, including
  stack-use-after-scope/return detection. Warnings are errors with no warning exemptions.
- CubeIDE GCC 14.3.1: 27 library/demo/check translation units compiled for Cortex-M7 with
  C++17 at `-O2` and `-Os`, without exceptions/RTTI and with warnings treated
  as errors. A minimal JSON consumer also linked with newlib-nano and enabled
  floating formatting. This was a link check, not execution on a board.
- All nine pre-existing codegen probes match pre-change checkpoint `55fbd481`
  byte for byte at both optimization levels (18/18 objects). The two new probes
  prove read-only storage and fixed ARM sizes for borrowed fields and owning
  command tables. The earlier factory comparison reduced a known free-function
  read from 304/288 bytes to a 4-byte direct branch (`-O2`/`-Os`). See the
  [factory codegen evidence](tests/README.md#signature-factory-and-command-codegen).
- [ARM CI runner](tests/run_arm_checks.py): the same compile/link checks pass
  with Ubuntu ARM GCC 13.2.1 and now run in GitHub Actions. Every codegen object
  is checked for startup initialization and writable data; exported constant
  tables are checked for read-only placement and expected sizes.
- Getter/Setter method binding rejects temporary owners even when the object
  template type is explicitly supplied. Four formerly accepted forms reproduced
  stack-use-after-return under ASan and now fail at compilation.
- Schema and value JSON escape all names/units, including quotes, backslashes
  and control bytes, and retain UTF-8. `catalog_names_unique` supports optional
  constexpr validation of the value-object keys.
- Serialization now rejects null output safely, stops reading after an output
  failure, preserves F32 precision and emits a JSON decimal point in a comma
  locale. U64/S64 formatting no longer needs `long long` support in printf;
  the installed CubeIDE newlib-nano configuration disables that support.
- Schema serialization/fingerprinting reject null catalog, field and unit
  metadata safely. The independent core anchor and compiled JSON calls both
  encode the exact layout tuple from which `telemetryAbiSignature` is derived;
  host and Cortex-M7 checks prove matching core/JSON archives link and mixed
  layouts do not. A 588-window H7S run of the layered serializer measured
  1032/976 bytes (`-O2`/`-Os`) and restored the original firmware byte for byte.
- [IndexCodegen.cpp](tests/IndexCodegen.cpp), built for Cortex-M7 at `-O2`
  and `-Os`, confirmed direct lookup without loops/helper calls and constant
  folding of known IDs. Both levels have actual bounds checks. The probe's
  constant tables/index are in `.rodata` with no startup initialization;
  Scalar is 16 bytes, Getter 8, Setter 8, FieldType 48, Field 96 (aligned to 32), Catalog 16 and CatalogIndex 8 bytes
  on ARM32.
- [RW32 measurements](tests/field_layout/h7s/RW32_RESULTS.md) cover the separate
  read/write metadata lines. Cache-line alignment is configurable through
  `TELEMETRY_FORCE_CACHELINE`; Field definitions are immutable and ABI revision
  5 requires a clean rebuild of consumers. The ABI guard adds no code to the
  hot path; all fourteen O2/Os ARM codegen probe objects remain byte-identical.
- The [exact compact-callback A/B](tests/field_layout/h7s/COMPACT_CALLBACK_RESULTS.md)
  retains the 96-byte Field stride while reducing Getter from 12 to 8 bytes.
  On the random 1024-row RAM profile at `-O2`, Scalar read improved 5.1%,
  float read 8.2% and U16 write 13.4%; the linked image shrank by 1696 bytes.
  The complete 1840 timing windows, image/object hashes and byte-exact firmware
  restoration are retained and pass the offline evidence verifier.
- Three live H7S command-layout A/B sessions compared the compact 24-byte row,
  full/half-line 32-byte rows and a reordered compact row. The production `-O2`
  path was fastest with the original compact layout; alignment was neutral on
  repeated execution and up to 0.38% slower on a 1024-row table, while compact
  reordering was 4.35% slower. The original firmware was restored and verified
  after every session.
- The layered refactor was compared with exact checkpoint `036d8e8` using the
  same CubeIDE GCC 14.3.1 invocation. All fourteen O2/Os probe object files are
  byte-identical, so the directory split and private-helper extraction changed
  no instruction, relocation or constant byte in the measured read/write/index
  and numeric paths. JSON object code changed because it gained the selectable
  64-bit string representation.
- Explicit/inferred known F32 reads fold to a direct getter branch. Float
  conversions retain float precision unless a double is requested. The
  storage comparison retains the former union's sizes and instructions in
  the measured paths; the std::visit comparison explains the direct tag
  dispatch retained for size-optimized builds.
- [DeclaredTypeCodegen.cpp](tests/DeclaredTypeCodegen.cpp) covers normalization
  on read and write. Matching native reads have no cast; F32-to-U16 uses one
  conversion after two range comparisons, without a double intermediate or
  a round/trunc function. Matching write probes also avoid initializing an
  empty Scalar and have no memset call. Bounds, truncation and F32 rounding
  remain effective when the requested read type is wider than declaredType.
- [EnumCodegen.cpp](tests/EnumCodegen.cpp) compares U16 fields with and without
  schema dictionaries and identical numeric limits. Matching read/write paths
  have the same numeric operations at both optimization levels; table
  addresses/offsets and instruction encodings differ. Runtime Field access
  never loads the schema callback. Enum names and field
  tables remain constant data; JSON dictionaries are produced only for schema requests.
- [LimitsCodegen.cpp](tests/LimitsCodegen.cpp): bounded reads branch directly
  to getters. Full-range U16 writes have no interval comparison; a custom
  10..20 interval uses subtraction and one unsigned comparison. F32 bounds
  use native F32 comparisons. A known rejected write folds to a constant result.

These ARM checks inspect compiled objects; they do not link firmware, run on
a board or measure cycles. Getter invocation is distinct from ID lookup;
the measured inlining limits are in the
[library notes](lib/telemetry/README.md#verification-and-cortex-m7-code-generation).
