# NUCLEO-H7S3L8 serial Inspector: retained local evidence

Author: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.

This bundle retains the original, byte-identical local receipts and text logs
for the first UART/DMA Inspector qualification on 28 October 2026. These are
recorded local observations; GitHub CI does not program or test the physical
board. The [manifest](manifest.json) hashes every retained measurement file.

## What was observed

| Evidence | Result |
| --- | --- |
| [Hardware receipt](hardware/summary.json) | NUCLEO-H7S3L8, device ID 0x485, COM6 at 115200; qualification passed |
| [Device report](hardware/device-report.json) | Four Fields, seventeen scalars; Voltage, exact U64, demonstration flag and nested Probe passed |
| [Actual QML screenshot](hardware/serial-inspector.png) | Native Windows Qt 6.10.1; eight endpoint rows and four value rows |
| Original internal Flash restoration | All 65536 bytes restored, programmer verification and independent readback SHA-256 matched |
| [Firmware build](firmware-build/summary.json) | CubeIDE GCC 14.3.1, ARM compile/link, 22164-byte Flash image; no linked allocator symbols |
| [GUI build used on hardware](gui-hardware-build/summary.json) | Windows Qt 6.10.1; two unopened lifecycle checks, no PTY or hardware test in this build receipt |
| [Final Windows serial build](serial-windows-final/summary.json) | Two unopened lifecycle checks; separate from the physical run |
| [Final GCC PTY suite](serial-gcc-final/summary.json) | 1204 checks, eight reads, three admissions; Qt 6.4.2 |
| [Final Clang PTY suite](serial-clang-san-final/summary.json) | 1204 checks; ASan/UBSan, with LeakSanitizer disabled for Qt globals |

Voltage values are demonstration constants, not ADC measurements. The physical
run checked discovery, four sequential FieldReads, COM reopening and a separate
actual QML window. It did not inject cable failures, establish sustained
DMA/cache reliability, or measure complete stack and CPU budgets.

## Exact source and binary identity

The implementation was first published as commit
`d8137af6a81557a41c636439171957ef1d5c1ea8`. The hardware GUI was built with the
serial changes in an uncommitted working tree based on
`a537988bb53292dca0c5c221f9d9008d5464be82`. The final Windows receipt records the
same baseline; the final Linux receipts record the later main merge commit
`c6ba1d83e973bfc6daf8fa201c977bd5f53f65a7`. A receipt's `source_head` is **not**
a claim that hardware ran a clean checkout of `d8137af`. The receipts retain
before/after input hashes for the actual builds; all final GUI inventories match.

The physical run used:

- ELF SHA-256: `7040c3b7f09a3980b64951218354dcb8e98514c687059c2627e9846ad398a798`.
- GUI SHA-256: `b0a665eaa9219d2629fece2abbda6a4ea4f3417305496b2ab0618fb955bf34f5`.
- COBS revision: `2e0abf260848fcb74e4a57b37532188047759643`.
- Original and restored Flash SHA-256: `a5903024dba85fab5121150ca8ad13482f97384aa450aab67413881991fb9456`.

The later final Windows GUI has SHA-256
`3e21d5385bbc232e26550f15b886b6f12a79b0a5309d97f9d266cfb20680eb92`.
Its 238-input inventory differs from the hardware GUI build only in `Main.qml`
(the footer now mentions UART as well as TCP) and `ci.yml` (offline CI gates).
All other recorded GUI inputs are identical. The hardware receipt's own source
inventory differs at publication only in the hardware runner README; the
additional offline mock controls were added after that run. No firmware or
hardware-path C++ logic changed after the physical qualification.

These historical receipts are intentionally frozen. Subsequent documentation,
evidence-verifier and CI changes do not change what the original run measured.

## Verify without hardware

From the repository root:

```sh
python -B tests/nucleo_h7s3l8/VerifyEvidence.py --self-test
```

The verifier checks retained file/log hashes and their cross-receipt identities,
the independent restoration result, the reported Fields and exact U64, COBS
revision and the explicitly recorded post-hardware GUI input changes. Negative
controls exercise corrupt and inconsistent evidence. It needs only Python's
standard library and never opens COM, ST-LINK or a programmer.

## Intentionally excluded artifacts

The original Flash backup, restored readback, tested ELF/BIN, compiler probe
binaries, GUI executables, shared libraries and build directories remain outside
the repository. Their identities may appear in original JSON, but references to
them are not bundled downloads. The retained text logs are mapped explicitly in
the manifest. JSON and logs retain the recording machine's local paths and board
identifiers for correlation; those paths are not portable build commands.

See the maintained [hardware runner documentation](../../../tests/nucleo_h7s3l8/README.md)
and [device example](../../../examples/nucleo_h7s3l8/README.md) for fresh builds
and the explicit backup/program/test/restore procedure.
