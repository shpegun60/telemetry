# Generic resource validation

Authors: Ruslan Kovtun (shpegun60), codexAi. MIT.

This C++20 suite exercises the flat filesystem and LIST/STAT/READ/WRITE
protocol independently of telemetry, PFR and any optional provider.

```sh
python tests/resources/run.py --build-dir build/resources
python tests/resources/run.py --cxx clang++-18 --sanitize --build-dir build/resources-san
python tests/resources/run.py --null-checks --build-dir build/resources-null
python tests/resources/run.py --cxx arm-none-eabi-g++ --arm --build-dir build/resources-arm
```

- `CoreCheck.cpp` retains provider, cursor, path, partial-buffer, LIST progress,
  packet-boundary and output-canary checks. LIST never calls `size()`.
- `Negative.cpp` has 34 intended compile-time refusals and an executed
  positive binding control. The runner verifies the particular diagnostic;
  a missing unrelated header cannot satisfy a lifetime refusal.
- `BytesFileCheck.cpp` checks the ready-made borrowed byte provider: every
  small offset/capacity combination, output canaries, EOF, u64 cursor edges,
  shared backing storage and overlapping reads in both directions. Its 122
  executed conditions are counted separately. `BytesFileNegative.cpp` rejects
  temporary arrays, conversion proxies and braced buffers; the host also
  checks the u32 size ceiling during constant evaluation.
- `FileViewCheck.cpp` executes 88 conditions for lazy enumeration, checked
  indexing, current size/capabilities, explicit read/write, borrowed lifetime
  through temporary views, independent forward iterators, empty ranges and
  invalid-file behavior. Capability checks cover read-only, write-only,
  read/write and invalid files without invoking size/read/write callbacks.
  Compile-time controls check constexpr capabilities, standard range concepts
  and descriptor privacy. `FileViewNegative.cpp` has 13 intended refusals for
  access through temporary owning tables, unsupported iterator operations,
  implicit bool conversion and private descriptor/iterator construction.
- The complete [three-file example](../../examples/resources/README.md) is
  built and executed with 23 counted conditions on the host and compiled for
  ARM. It adds a writable custom provider to ordinary `BytesFile` entries.
- `NoHeapCheck.cpp` rejects C++ allocation and repeatedly serves fixed
  providers through both filesystem and packet APIs.
- `ArmProbe.cpp` checks actual Cortex-M7 descriptor sizes/offsets, constant
  storage, direct known-provider dispatch and a linked protocol image.
- `stack_check.py` bounds every individual protocol frame at 192 bytes and
  rejects malformed, missing or excessive reports. This is an individual
  frame gate; whole-chain peaks are measured separately on H7S.

`summary.json` records counted commands, intended refusals, executed
conditions and execution scope. ARM compile/link results are not MCU runs.
Compiler, diagnostics, disassembly, sections and symbol logs are retained
beside the summary. Linked ARM allocation/formatting checks use the same
newlib-aware controls as the final telemetry qualification suite.

The generic host suite currently executes 9,465 conditions, with 64 intended
compile-time refusals. ARM has 63 refusals: the external array larger than
u32 used by the last host control cannot be declared on its 32-bit target.

On 2026-10-04 the complete host suite passed under WSL Ubuntu Clang 18.1.3
with AddressSanitizer, UndefinedBehaviorSanitizer and float-cast-overflow
checks: 88 commands, 64 intended refusals and all 9,465 executed conditions.
That run includes the current `FileView`, `BytesFile` and
`resource::protocol` interfaces. Its logs and summary were written outside
the checkout under
`C:/Users/admin/Documents/telemetry-validation/20261004-final/resources-capabilities-clang-san`.
It is host evidence; no device was connected or image executed on an MCU.

The optional v3 provider suite is
[`tests/structured/resources`](../structured/resources/README.md). It owns
PFR-dependent header checks, independent descriptor/value oracles, bounded
reader frames, short buffers, lifetime refusals and compiled reader ABI
checks under GC/LTO. It also builds `DeviceCheck.cpp` with the migrated
application's two-file resource facade. Generic resource users do not need
those dependencies.

Scalar/v2.1 fixtures and their measurements are historical. Their source is
available in the pre-migration Git tree `389c995`; they are not part of the
final v3 acceptance matrix. The final hardware evidence belongs to
[`tests/structured/mcu/h7s`](../structured/mcu/h7s/README.md).
