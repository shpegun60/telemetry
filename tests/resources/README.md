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
