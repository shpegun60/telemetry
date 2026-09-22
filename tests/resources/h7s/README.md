# H7S binary-resource measurements

Authors: Ruslan Kovtun (shpegun60), codexAi. MIT.

The harness compares the exact `f1cfbd891ef2f0c23f0fb2dae70be8ed4bd6fcaf`
baseline with a snapshot of the current library. It reuses the copied Cube
scaffold described in [the field benchmark](../../field_layout/h7s/README.md),
without modifying the original COBS project. Use the CubeIDE compiler.

```powershell
python tests/resources/h7s/run.py --arm-cxx <path-to-arm-none-eabi-g++.exe> --output build/resource-measurement
python tests/resources/h7s/run.py --arm-cxx <path-to-arm-none-eabi-g++.exe> --output build/resource-live --run --serial 002A001F3033510135393935 --port COM6
```

The first command only builds. `--run` backs up the entire 64 KiB internal Flash,
verifies each programmed image, then restores the backup and compares a fresh
read-back hash. All images must fit that region. Option bytes and external memory
are not programmed. A power or host-process interruption requires restoration
from the retained `before.bin`; a failed restore is never reported as success.

The fixture contains 256 catalogs, two fields per catalog, and one command with
F32/enum parameters. It measures first/last field-schema and command-schema READ,
first/last fixed-width value READ, and the original sequential enum and parameter
visitors. Payload checksums must agree between revisions. Metadata must invoke
zero getters and each value request exactly one getter. DWT measures 256 calls
per window, five windows per operation, with interrupts masked and caches warmed.
The reported time includes dispatch, bounded encoding and checksum consumption.

The independent 16 KiB DTCM PSP buffer measures the complete nested call path,
not only the outer READ frame. Two fill patterns and a 512-byte positive control
check the watermark; a 256-byte guard catches a used-up test stack. UART output
and integer formatting occur outside the measured calls. The PSP trampoline is
shared with [the JSON stack test](../../json_stack/README.md).

The output directory retains source/object/image hashes, UART records, compiler
logs, disassembly, stack-usage reports and `receipt.json`. `completed` and
`restored_and_verified` must both be true before a run counts as evidence. Failed
attempts are diagnostics, not cycle measurements. No board result is inferred
from host timing or instruction counts.
