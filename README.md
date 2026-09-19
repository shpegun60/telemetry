# Telemetry playground

A Qt Widgets application for exercising the standalone C++17 telemetry
library copied from the analyzer. All values in this application are
simulated; it does not connect to an instrument.

Repository: [shpegun60/telemetry](https://github.com/shpegun60/telemetry).
Clone the complete project, then open `telemetry.pro` in Qt Creator:

```sh
git clone https://github.com/shpegun60/telemetry.git
```

The library, delegate and magic_enum dependencies, demo and checks are all included in this
repository. No analyzer firmware checkout or Git submodule is required.

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
  telemetry.pri                    reusable qmake include
  Telemetry*.h, TelemetryJson.cpp   all library headers and implementation
lib/delegate/
  delegate.pri, tiny_delegate.hpp  pinned tiny_delegate v1.1.0 dependency
  LICENSE, README.md               license and source revision
lib/magic_enum/
  magic_enum.hpp, magic_enum.pri   pinned v0.9.8 for optional enum schema metadata
  LICENSE, README.md               upstream license and source revision
tests/                             standalone checks without Qt
build/                            generated files, ignored by git
```

Open `telemetry.pro` in Qt Creator and build with the installed Qt 6 MinGW
kit. The project includes `lib/telemetry/telemetry.pri`; no separate library
build step is required; its `.pri` includes the sibling dependency files.
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

The table displays twenty fields with stable global IDs and refreshes every 500 ms. The schema and
value JSON are shown below it; Pause/Resume stops and starts the simulated
updates. `--smoke-test` closes the application after 1.2 seconds, so it can
also be launched with Qt's `-platform offscreen` for a short startup check.

Add static points to `meterFields` in
[DemoCatalog.cpp](app/demo/DemoCatalog.cpp). Its five Ua rows deliberately
read the same changing voltage through different bindings:

| Field | Getter expression |
| --- | --- |
| `Ua` | `[]() noexcept { return meter.voltage; }` |
| `UaFunction` | `readUa` |
| `UaAddress` | `&readUa` |
| `UaBind` | `Getter::bind<&readUa>()` |
| `UaMethod` | `Getter::bind<&Meter::readVoltage>(meter)` |

`readUa` is an ordinary free function with signature `float() noexcept`.
All five entries belong to the same constexpr table. The current, power and
counter rows keep `+[]` examples; the integer group keeps explicit Scalar
factories. Getter supports both, retaining noexcept targets and empty-to-Null behavior.
The `DemoCatalog` constructor shows runtime method bindings in `sensorFields_`. The owner
stores the sensor, field array and catalogs together and cannot be moved
or copied.

IDs pack a 16-bit group and a 16-bit field position. Meter is group 0
(IDs `0..9`); sensor is group 1 (IDs `65536..65537`); integer examples are
group 2 (IDs `131072..131079`). The integer rows show unsigned maxima and
signed minima for every 8/16/32/64-bit type. Their display reads the simulated
sources directly, keeping all U64/S64 digits without conversion to double.
The sources stay stable while the table and JSON are refreshed.
The `Mode` row uses `enumType<Mode>()`: its value remains U16 while schema
JSON adds `"enum":{"0":"Off","1":"Auto","2":"Manual"}`. Its write interval
is 0..2 and default is Auto (1). No dictionary check runs during lookup, read
or write. See the [enum contract and large-code
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
VoltageLimit (ID 8) and Mode (ID 9) are writable; other fields are read-only.
VoltageLimit declares write limits 1..1000 and default 250:

```cpp
const auto result = demo.index().write(telemetry::makeId(0, 8), 275);
// Applied; an ordinary int is converted to the field's F32 before its setter.
```

`numericType<float>(1, 1000, 250)` defines those limits at compile time.
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
Clang sanitizers and an offscreen Qt application check.
Library integration and contracts are in [lib/telemetry/README.md](lib/telemetry/README.md).

Verified after numeric, lifetime-contract, serialization, enum and write-limit review
on 2026-09-19:

- Qt 6.10.1 / MinGW 13.1.0: Release application build and offscreen startup;
  99/99 core, 87/87 write/getter, 87/87 read, 17/17 JSON, 121/121 numeric
  oracle, 45/45 enum and 66/66 limits checks passed with C++17 (522 total).
  C++20 also checks char8_t (88/88 write/getter and 88/88 read; 524 total).
  Twenty-seven expected compilation failures cover invalid bindings/reads,
  temporary arrays, invalid Scalar access, enum contracts and invalid limit definitions.
  Standalone public headers compile; fast-math and finite-math-only builds
  are rejected. The Qt table also
  showed the exact boundary values for all eight integer types, matching
  the raw value JSON, including `UINT64_MAX` and `INT64_MIN`.
- Clang 18 C++17 with ASan/UBSan and float-cast-overflow checks: all seven suites
  passed with warnings treated as errors and no warning exemptions.
  Clang C++20 Release passed the same suites and compilation checks.
- CubeIDE GCC 14.3.1: library, demo and checks compiled for Cortex-M7 with
  C++17 at `-O2` and `-Os`, without exceptions/RTTI and with warnings treated
  as errors. A minimal JSON consumer also linked with newlib-nano and enabled
  floating formatting. This was a link check, not execution on a board.
- Serialization now rejects null output safely, stops reading after an output
  failure, preserves F32 precision and emits a JSON decimal point in a comma
  locale. U64/S64 formatting no longer needs `long long` support in printf;
  the installed CubeIDE newlib-nano configuration disables that support.
- [IndexCodegen.cpp](tests/IndexCodegen.cpp), built for Cortex-M7 at `-O2`
  and `-Os`, confirmed direct lookup without loops/helper calls and constant
  folding of known IDs. Both levels have actual bounds checks. The probe's
  constant tables/index are in `.rodata` with no startup initialization;
  Scalar is 16 bytes, Getter 12, Setter 8, FieldType 40, Field 80, Catalog 16 and CatalogIndex 8 bytes
  on ARM32.
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
