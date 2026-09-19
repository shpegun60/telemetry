# Field layout A/B/C experiment

This is a local compiler/layout experiment against stable commit
`688ae7eee3f95f8aca312c24bb38fc79d8053907`. It needs no board or Qt runtime.
The historical A/B/C controls use that pinned Git commit, validated against
`baseline.json`, even after the production library changes. Candidate headers
are generated only inside the requested build directory, with a readable
`Field.diff`. `Current` instead uses the current checkout's library and tests.

| Variant | Change |
|---|---|
| A | Original Field, including its original read/write methods. |
| B | Getter, Setter, FieldType, id, name, unit; unchanged read/write methods. A constexpr constructor retains positional row initialization and defaults. |
| C | 32-byte alignment; Getter, Setter, cached type/flags in the first line; all definition members const; assignment disabled. Numeric validation uses cold bounds only for restricted writes. |
| A32 | A with only `alignas(32)`: 96-byte stride, original member order. |
| B32 | B with only `alignas(32)`: 96-byte stride, compact read prefix. Selected for production. |
| B64 | B with `alignas(64)`: 128-byte stride. Measured as a control, excluded from the production choice. |
| Current | Unmodified library and tests from the current checkout. |

C is a combined layout and validation-path experiment. Its results do not
isolate alignment alone. B isolates member reordering in reads/writes, while
its constructor necessarily changes `is_aggregate_v<Field>`.

`type`, `flags` and `declaredType` in C are all immutable, preventing independent
changes after construction. Copy/move construction works; assignment does not.
Assignment is implicitly disabled; explicit deleted declarations made Clang 18
report `is_trivially_copyable_v<Field> == false`. Both forms reject assignment,
but the implicit form retains the required trait on all tested compilers.
Default construction, partial `{id}` initialization, ordinary six-member rows
and reads of `field.declaredType.minimum()/maximum()/hasEnum()` are retained.
General aggregate/designated initialization and member mutation are not retained.

## Run locally

From this experimental checkout, Python 3.9+ and the CubeIDE compiler are sufficient
for the ARM measurements. Resolve the installed kit rather than using CubeCLT:

```powershell
$armCompiler = (Get-ChildItem 'C:/ST/*/STM32CubeIDE/plugins/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32*/tools/bin/arm-none-eabi-g++.exe' | Select-Object -First 1).FullName
python tests/field_layout/run.py --mode arm --arm-cxx $armCompiler --build-dir build/field_layout_experiment/arm
python tests/field_layout/run.py --mode arm --variants A32 B32 --arm-cxx $armCompiler --build-dir build/field_layout_experiment/arm-aligned
```

Select the desired CubeIDE executable explicitly when several installations
exist. Its sibling `objdump`, `objcopy` and `nm` are used automatically.
`--build-dir` also accepts an absolute path or a sibling project's build folder.
The recorded local run writes to `telemetry/build/field_layout_experiment` in
the original workspace, while this checkout stays on the experiment branch.

Host behavior checks use GCC/Clang and do not need an ARM compiler:

```sh
python3 tests/field_layout/run.py --mode host --host-cxx g++ --std c++17 --build-dir build/field_layout_experiment/gcc17
python3 tests/field_layout/run.py --mode host --host-cxx g++ --std c++20 --build-dir build/field_layout_experiment/gcc20
python3 tests/field_layout/run.py --mode host --host-cxx clang++-18 --sanitize --build-dir build/field_layout_experiment/clang17-sanitized
```

On Windows, put the selected MinGW `bin` directory in PATH and use `python`.
Set `TELEMETRY_TEST_LOCALE=German_Germany.1252` on Windows or `de_DE.UTF-8` on
Linux, with that locale installed. The runner rejects skipped numeric/locale
coverage. Its numeric oracle needs at least 64 mantissa bits in `long double`.
`--mode all` runs ARM and host checks together.
`--variants` selects any subset from the table; the default is A B C. Use
separate output directories for different selections. The repository must
retain the pinned baseline commit in its Git object database.

## What is measured

- `baseline.json` pins the stable library/dependency sources by normalized
  SHA-256. Historical sources are read from Git, with no checkout mutation.
  A missing commit or mismatched source hash aborts the experiment.
- ARM C++17, Cortex-M7, Thumb, FPv5-D16 hard float, `-O2` and `-Os`, without
  exceptions/RTTI, with warnings as errors. No LTO or board linker script.
- Layout words are extracted from the ARM object with `objcopy`, not measured
  using the host ABI. They include every requested size/alignment/offset.
- The same 1024-row mixed-type fixture and 21 exported functions are compiled
  for every variant. Repeated names in the large fixture are intentional; the
  first eight distinct rows form the separate valid JSON fixture.
- Runtime IDs through a passed index and through a fixed index; Scalar/typed
  reads; float/U16 writes; known-ID reads/writes; bounded F32/U16 writes;
  full-range U16; equal-limit enum/plain U16; normalization and read-only cases.
- Section totals include callback/variant helpers. Per-function sizes include
  embedded literal pools; static instruction counts exclude them. These are
  whole-function counts across all branches, not executed paths or cycles.
- Empty writable storage and no startup constructor sections are required.
  The Field table must be read-only with exactly `1024 * sizeof(Field)` bytes.
  Every variant also links the same minimal newlib-nano JSON consumer. Expected
  nosys stub warnings are retained in its log. The ARM programs are not executed.

`ARM_RESULTS.md` and `arm-results.json` contain the measurements. Every variant
has `O2/Probe.asm`, `Os/Probe.asm`, Json disassembly, section/symbol listings,
compiler/link logs, generated headers and adapted tests. Relocation annotations
in `.asm` identify calls correctly: a bare disassembly can misleadingly label
an unresolved call with the first local symbol at address zero.

## Behavior and API checks

The seven stable suites run against each generated candidate. A uses unchanged
tests. B/C reverse the two explicit aggregate-trait assertions; they still
require trivial copying. C changes three setup-time member assignments into
construction of equivalent replacement definitions, and expects its new ARM
size. No numeric, lookup, range, JSON or callback assertion is removed.
A32 retains A's aggregate contract; B32/B64 retain B's contract, with their
expected ARM sizes updated. Current runs its own tests without adaptation.

The original 35 expected compile failures run for each variant. C has five
additional rejection cases: changing cached type, flags or declaredType, and
copy/move assignment. `Contracts.cpp` checks default/partial/copy construction,
public metadata reads, bounded writes and equal complete JSON documents.
Sanitized runs enable address, undefined-behavior and float-cast-overflow
checks, including stack use after scope/return.

## Interpreting the result

See [RESULTS.md](RESULTS.md) for the measured comparison and the limits of the
conclusions. A compact Field prefix is a property of the layout; actual cache
misses, bus transfers and latency depend on execution and memory placement.
No cycle numbers or throughput gains are inferred from object size.

The later [H7S hardware results](h7s/RESULTS.md) measure actual DWT cycles and
explain the B32 production decision. [The hardware runner](h7s/README.md)
uses a local copy of the COBS Cube scaffold and restores the board image.
