# MCU qualification: host, offline ARM and separate H7S execution

Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.

`run.py` executes the actual Mixed/Scale probe bodies on a host and compiles,
links and inspects Cortex-M7 images. It has no device, serial or programmer
integration. The separate [H7S runner](h7s/README.md) establishes actual
cycles and observed whole-probe-chain stack in its [results](h7s/RESULTS.md).

```sh
python tests/structured/mcu/run.py --cxx g++ --build-dir build/mcu-host
python tests/structured/mcu/run.py --cxx g++ --null-checks --build-dir build/mcu-null
python tests/structured/mcu/run.py --cxx clang++-18 --sanitize --build-dir build/mcu-san
python tests/structured/mcu/run.py --arm --cxx arm-none-eabi-g++ --build-dir build/mcu-arm
python tests/structured/mcu/receipt.py verify tests/structured/mcu/local-receipt.json --self-test
```

The default matrix is O2/Os/Og. Every runner records compiler/flags, exact
command results, LF-normalized inputs and compiler stack frames; changing
an input during the run refuses the evidence. ARM adds real linked symbols,
sections and disassembly and executes zero conditions.

## Retained scenarios

- Mixed executes exactly 12,230 conditions per optimization and reports the
  unchanged 97-check multi-TU consumer separately. It covers exact Config
  Fields/Services, direct/local/global access, bound slots, visitors, readAs,
  encoded Field/Command/Service, arrays/structs, 4 KiB request/response paths,
  insufficient Workspace refusal, every descriptor cursor and Values.
- Scale executes exactly 2,316 conditions per optimization: 128 distinct
  targets and their direct indexed, direct known, local/global, named/lambda
  visitor, readAs and encoded paths under same/sequential/shuffled IDs.
  Invalid catalog/entry/max-u32 IDs refuse before callbacks. Only the old
  local/Scalar controls were retired after their baseline was retained.
- Every large Service response byte is compared, including in-place wire
  input/output. Workspace leases release scratch on both success and refusal.
- Local/global release instructions must match direct C++ or contain a
  resolved compiler tail forward to that same body. Raw and effective
  comparisons remain separate; a forward is an extra branch. Two positive
  and five changed-code/target controls exercise the parser.
- Null-check mode can retain target-address tests; its difference from
  direct C++ is recorded. Local/global must still agree. Og streams are
  recorded without imposing release instruction identity.

## Final-tree offline evidence

The [local receipt](local-receipt.json) records seven actual compiler/mode
roles with exactly 138 shared LF inputs and 42 configurations.
These migration runs observed HEAD `389c995` with changed captured sources;
`source_dirty=true` is preserved. They are not a clean-SHA or hardware claim.
Each host executes 43,638 conditions, for 130,914 across three host roles.

| Role | Successful tool commands | Executed conditions |
| --- | ---: | ---: |
| mingw13 | 52 | 43638 |
| gcc13-null | 52 | 43638 |
| clang18-sanitized | 52 | 43638 |
| cubeide14 | 67 | not executed |
| cubeide14-null | 67 | not executed |
| arm13 | 67 | not executed |
| arm13-null | 67 | not executed |

The independent verifier requires every role, pinned compiler driver,
Cortex-M7 hard-float ABI flags, normal/null/sanitized mode, all configurations,
counts, source/report hashes and real-symbol controls. Its 32 mutations
include omitted/substituted roles and changed counts/flags/frames.
Raw reports stay in `build/stage20/offline-<role>`; their recorded digests
allow comparison with retained files but do not recreate missing artifacts.

Each final library frame is checked, including files under `lib/telemetry`.
There is no directory-based legacy exemption. Individual frames are bounded
at 256 B, except multi-TU consumer roots with 512 B release/768 B Og bounds.
The real linked-symbol control must retain and refuse `_malloc_r` and
`_printf_r`; all final images also reject formatting, allocation, old Scalar
and startup metadata constructors. Freestanding failed contracts use a trap.

Actual CubeIDE14 individual root frames:

| Probe | O2, B | Os, B | Og, B |
| --- | ---: | ---: | ---: |
| 4 KiB encoded Field | 192 | 168 | 168 |
| 4 KiB encoded Command | 176 | 152 | 152 |
| 4 KiB encoded Service | 216 | 192 | 192 |

These are individual compiler frames, not full call-chain bounds. An owning
native Big return deliberately places a large result on the application
stack; the H7S probe measures its complete chain separately. Equal instruction
bytes do not prove equal cycles at different function addresses.

The pre-unification [offline/hardware proof](../../../doc/evidence/pre-unification/README.md)
retains its original counts, old Scalar comparison and source identity.
The final CI must qualify the published SHA independently of these local runs.
