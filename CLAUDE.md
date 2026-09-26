# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A standalone repository (`github.com/shpegun60/telemetry`) with two embedded-first
C++ libraries, a Qt playground that exercises them with simulated values, and a
check suite that is most of the code:

- `lib/telemetry/` - C++17 typed fields and commands addressed by position. No Qt,
  RTOS or HAL dependency; it targets Cortex-M7 firmware (no exceptions, no RTTI,
  newlib-nano) as well as hosts.
- `lib/resource/` - C++20 flat resource table, with an optional packet protocol
  (`protocol/`) and telemetry adapters (`telemetry/`) that serve schema, commands
  and values as binary files.
- `lib/delegate/`, `lib/magic_enum/` - vendored upstream headers kept byte for
  byte (`-text` in `.gitattributes`). Do not edit them.
- `app/` - Qt 6 Widgets playground. `web/telemetryBinary.js` - browser decoder
  for the binary files.
- `tests/` - host suites, numbered compile-fail programs, Cortex-M7 code-generation
  probes, resource checks, and retained board measurements under `tests/*/h7s/`.

The library started as a copy of the power-analyzer firmware's `app_core/telemetry`
(the parent workspace, where this repository is checked out beside it). It has
moved far past that copy - the firmware still carries the original four-file core -
and nothing here flows back automatically.

## Commands

Run from this directory. Every runner writes objects, executables and one log per
step only to the `--build-dir` you give it; keep that under `build/` (git-ignored).

On Windows use the Qt MinGW compiler with its `bin` first on PATH, so the test
executables find their runtime DLLs. Resolve the directory rather than trusting
the one below; it carries a version.

```powershell
$env:PATH = 'C:\Qt\Tools\mingw1310_64\bin;' + $env:PATH
python tests/run_checks.py --cxx g++ --std c++17 --build-dir build/checks-gcc17   # all suites, rejection cases, ABI archives (~4 min)
python tests/run_checks.py --cxx g++ --std c++20 --build-dir build/checks-gcc20
python tests/resources/run.py --build-dir build/resources                         # C++20 resource checks; --node node adds the JS decoder
```

- Sanitizers need Clang; MinGW has no ASan:
  `python3 tests/run_checks.py --cxx clang++-18 --std c++17 --sanitize --build-dir build/checks-san`,
  and the same `--cxx clang++-18 --sanitize` for `tests/resources/run.py`. On this
  machine WSL (Ubuntu 24.04) has clang++-18, but no Node and no de_DE locale.
- The decimal-comma JSON check is skipped when its locale is missing. Set
  `TELEMETRY_TEST_LOCALE=de_DE.UTF-8` (Linux) or `German_Germany.1252` (Windows)
  to make a missing locale fail instead.
- Cortex-M7: `python tests/run_arm_checks.py --cxx <arm-none-eabi-g++> --build-dir build/arm`
  and `python tests/resources/run.py --arm --cxx <arm-none-eabi-g++> --objdump <objdump> --size <size> --build-dir build/resources-arm`.
  Locally use the STM32CubeIDE compiler
  (`C:\ST\STM32CubeIDE_*\...\gnu-tools-for-stm32.*\tools\bin`); CI uses Ubuntu's
  arm-none-eabi GCC, which is not the firmware compiler.
- Qt playground: open `telemetry.pro` in Qt Creator, or from `build/qt` run
  `qmake ../../telemetry.pro CONFIG+=release` and `mingw32-make` (Qt kit `bin` on
  PATH too), then `release\telemetry_playground.exe -platform offscreen --smoke-test`:
  exit 0 passes, 2 or 3 is a failed check. `tests/resources/no_json.pro` is the
  separate target proving the resource adapters build with `CONFIG += telemetry_no_json`.
- Retained board evidence is checked offline by the `verify.py --self-test`
  commands listed in `.github/workflows/ci.yml`.

There is no suite filter. To run one suite, compile it the way `tests/run_checks.py` does:

```powershell
g++ -std=c++17 -Wall -Wextra -Werror -pedantic-errors -Ilib/telemetry -Ilib/delegate -O2 tests/TelemetryReadCheck.cpp lib/telemetry/abi/TelemetryAbi.cpp lib/telemetry/serialization/TelemetryJson.cpp lib/telemetry/serialization/TelemetryCommandJson.cpp -o build/one/TelemetryReadCheck.exe
build/one/TelemetryReadCheck.exe
```

One rejection case is the same flags plus
`-DTELEMETRY_READ_FAIL_CASE=5 -fsyntax-only tests/TelemetryReadCompileFail.cpp`.
It has to fail *with the diagnostic* `tests/run_checks.py` lists for that number;
failing for another reason does not count. Most suites also have a
`tests/telemetry_*_check.pro` for Qt Creator (`TelemetryMetadataCheck` has none).

CI (`.github/workflows/ci.yml`) runs GCC and Clang 18 at C++17 and C++20, a
sanitized Clang C++17 job, the evidence verifiers, the Cortex-M7 checks and the
offscreen Qt build.

## Architecture

### Telemetry

- Layers: `core/` (Scalar, checked conversions, packed IDs, cache-line policy),
  `field/` (Getter/Setter, FieldType with limit and enum metadata, immutable
  `Field`, `FieldTable`), `catalog/` and `command/` (grouped tables and their
  indexes), `slot/` (late binding), `serialization/` (optional JSON). `Telemetry.h`
  includes everything except JSON; `detail/` is private.
- Identity is positional. A packed ID is `makeId(group, position)`, 16 bits each.
  No descriptor stores its ID, so reordering or deleting a row changes public IDs;
  retire rows with `reservedField()` / `reservedCommand()`.
- One definition serves three access levels that must stay equivalent: local
  compile-time position (`meterFields.read<2>()`), global compile-time ID
  (`fields.read<makeId(0, 2)>()`), and runtime ID from a transport
  (`fieldIndex.read(id)`, `commandIndex.execute(id, scalars, count)`).
- Tables are constant data: `constexpr`/`constinit` definitions whose owners,
  strings and arrays must outlive every reader at stable addresses. Mutable state
  lives in the owners. `OwnerSlot`, `FunctionSlot` and the context and delegate
  slots let a constant table reach an object or callback bound later. The library
  adds no synchronization; the owner provides it, and any cross-field snapshot.
- ABI guard: `abi/TelemetryAbi.h` puts the whole in-memory layout (ABI revision,
  cache-line size, pointer size, every descriptor's size, alignment and member
  offsets) into the template arguments of a link symbol. Objects built with a
  different layout reference a different symbol and fail to link. The JSON entry
  points and the resource adapter constructors carry the tag; inline-only code
  opts in with `requireTelemetryAbi()`.

### Resource

- Core: `resource::filesystem(resource::file("/path", provider), ...)`; identity is
  again the position. A provider has `size()` plus `read(cursor, output)` and/or
  `write(cursor, input, final)`. The transfer contract (cursor progress,
  EOF/complete, error returns) is in `lib/resource/README.md`.
- `protocol/`: LIST/STAT/READ/WRITE packets in explicit little-endian; one complete
  request in, one reply out. Framing, retries and checksums belong to the transport.
- `telemetry/`: three read-only files, `/telemetry/schema.bin`,
  `/telemetry/commands.bin` and `/telemetry/values.bin`. Values decode against the
  schema they match; the link is a 64-bit FNV-1a semantic fingerprint computed
  once at construction.

Three version numbers move independently: the in-memory ABI
(`telemetryAbiVersion`, 8), the binary resource format (2.1,
`lib/resource/telemetry/BinaryFormat.hpp`) and the packet protocol (v1).
Field-schema JSON has its own `meta.formatVersion`.

## Changing things

- Descriptor layout: a change to any type in the `CurrentAbiTag` tuple must be
  reflected in that tuple. Bump `telemetryAbiVersion` when meaning changes without
  a size or offset change (that is what ABI 8 was), and keep a frozen caller in
  `tests/abi/` that must still fail to link. Every consumer then needs a clean rebuild.
- Wire format: one change spans `lib/resource/telemetry/`, the hand-written goldens
  in `tests/resources/Golden.hpp` (never regenerated from the encoder),
  `web/telemetryBinary.js` and `tests/resources/DecoderCheck.mjs`. The decoder
  accepts exactly one version, and version bytes are part of the fingerprint, so a
  version change alters every fingerprint.
- Rejection cases: a new case needs its `#if ..._FAIL_CASE == N` block in the
  `tests/*CompileFail.cpp` file and its number with the expected-diagnostic regex
  in `tests/run_checks.py`.
- Gates: the ARM probes check instruction streams, `.rodata` placement, absence of
  startup initialization and writable data, absence of heap, software 64-bit
  multiply and text formatting in resource objects, and per-frame stack budgets
  (`tests/resources/stack_check.py`). When one fails, report the regression; do
  not loosen the gate.
- Warnings are errors in every runner (`-Werror -pedantic-errors`); fast-math and
  finite-math builds are rejected at compile time on purpose.
- `tests/*/h7s/run.py --run` programs a NUCLEO-H7S3L8 over ST-LINK, backing up and
  restoring its 64 KiB internal Flash. Do not run it unless asked. The receipts and
  CSVs there are byte-exact evidence; do not reformat them.
- READMEs are part of each change: commits touch code, tests and the affected
  README together, and results are stated at the strength of the evidence
  ("compiled-object observation, not board timing"). Several counts in the READMEs
  are maintained by hand; check them against the runners before quoting them.
- Only resource code has a formatter config:
  `clang-format --style=file:tests/resources/.clang-format -i <file>`. Do not apply
  it to the telemetry core or the vendored headers.
- Commit messages are one imperative subject line with no body, e.g.
  `Harden binary command and resource protocol semantics`.
