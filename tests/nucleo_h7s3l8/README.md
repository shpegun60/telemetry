# NUCLEO-H7S3L8 hardware qualification

Author: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.

The first physical run's original receipts, logs and QML screenshot are retained
in [the public evidence bundle](../../doc/evidence/inspector-serial-h7s-2026-10-28/README.md).
They include build identities and the independent Flash restoration result, but
exclude firmware backups and binaries. Verify them offline with:

```sh
python -B tests/nucleo_h7s3l8/VerifyEvidence.py --self-test
```

`RunHardware.ps1` is an explicit hardware runner for one identified board, not an
automatic CI test. It programs test firmware, runs the read-only serial Inspector,
and restores the original firmware even when the test fails. Ordinary host tests
must use the offline mock controls below instead of physical tools or ports.

## Offline runner controls

`CheckRunner.ps1` is safe for ordinary Windows CI: it uses file-backed mock
executables and shadows PnP enumeration. It does **not** invoke CubeProgrammer,
ARM tools, Qt, ST-LINK, USB or COM. Its only build dependency is the Windows .NET
Framework C# compiler and Python 3. The valid ARM ELF fixture is generated locally; no board or
previous firmware artifact is needed.

```powershell
powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass `
  -File tests/nucleo_h7s3l8/CheckRunner.ps1 `
  -ExternalOutput "$env:RUNNER_TEMP\nucleo-runner-controls"
```

The twelve maintained controls cover successful restoration, failed headless
check, failed GUI, GUI timeout and owned-process termination, partial programming
failure, three failed restoration attempts, independent readback mismatch despite
programmer verification, wrong device identity, and four malformed/off-target ELF
images. Invalid ELF must be rejected before any external tool command. Mock
receipts are explicitly marked as simulations; they are not hardware evidence.
The summary binds results to the current hardware runner, control source, mock
source/executable and generated ELF hashes.

The suite retains those twelve Inspector controls and adds nineteen ReadMany
controls: a valid frozen CLI/report, missing or malformed reports, a false result,
missing batch sizes, altered hashes/capabilities/sessions, incomplete recovery,
invalid item statuses, duplicate IDs, incorrect mixed results, owned-process
timeout, a failing CLI exit, partial programming, successful restoration retry,
failed restoration, wrong readback, and mutually exclusive checker arguments. The thirty-one runner controls
also execute the report verifier's in-memory positive and negative controls.

```sh
python -B tests/nucleo_h7s3l8/VerifyReadManyEvidence.py --self-test
```

## Physical board run

The fixed profile is NUCLEO-H7S3L8, ST-LINK serial
`002A001F3033510135393935`, device ID `0x485`, 64-KiB internal flash, and ST-LINK VCP
`COM6` at 115200 baud. The runner requires the COM device's USB parent to contain
that exact serial and VID/PID `0483:3754`; it never selects the first probe or an
arbitrary COM port. No other application may own the serial port or program/debug
this board during the run.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests/nucleo_h7s3l8/RunHardware.ps1 `
  -Elf C:\external-build\nucleo\telemetry-nucleo-h7s3.elf `
  -GuiExe C:\external-build\inspector\telemetry-inspector-qml.exe `
  -ExternalOutput C:\external-evidence\nucleo-serial-unique-run
```

The output directory must not exist and must be outside the Telemetry checkout.
`-Programmer` and `-ArmTools` can explicitly select installed ST tools. Automatic
tool lookup uses CubeProgrammer under `C:\ST\STM32Cube` and an unambiguous
CubeIDE 2.0.0 GCC directory. The GUI must have its Qt runtime and QML dependencies
available beside it or through its usual environment.

Use `-ValidateOnly` for an offline ELF/tool/input check. This mode does not list or
connect to ST-LINK, enumerate/open COM, reset, back up or program a board. Its
receipt explicitly does not claim hardware qualification.

## Explicit ReadMany qualification

The separate `-ReadManyExe` parameter set selects the UART CLI. It cannot be
combined with `-GuiExe`; this mode performs no Inspector/QML invocation. It uses
the same fixed board identity, full internal-flash backup and `finally` restore
with independent readback as the Inspector mode.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests/nucleo_h7s3l8/RunHardware.ps1 `
  -Elf C:\external-build\nucleo\telemetry-nucleo-h7s3.elf `
  -ReadManyExe C:\external-build\read-many\telemetry-read-many-uart.exe `
  -CobsRoot C:\Users\admin\Documents\my_workspace\Qt\COBS `
  -ExternalOutput C:\external-evidence\nucleo-read-many-unique-run
```

The runner snapshots the CLI executable and records its original/frozen hashes.
The frozen copy runs with the original executable directory on its child process
PATH so its Qt runtime remains available. Source hashes include the CLI, serial
transport/bootstrap, firmware profile and library sources, and are compared again
after restoration. The input ELF/tool checks and `-ValidateOnly` also work with
this parameter set. `-ReadManyTimeoutSeconds` bounds the owned CLI process;
`-Python` can explicitly select the independent report verifier's interpreter.
`-CobsRoot` is mandatory for ReadMany: the receipt binds the external COBS
`src` and `libs/spsc` source hashes to the read-only `rev-parse HEAD` identity,
checked again after restoration. The offline suite substitutes a file-only
COBS source fixture and mock Git executable for these operations.

`VerifyReadManyEvidence.py` requires the CLI's schema-1
`nucleo-h7s3l8-read-many` report, exact port/115200 baud, canonical 24-byte `TRM1`
capability, a descriptor SHA-256, and strictly increasing nonzero sessions. The
four cases must be exactly 8, 12, 16 and 32 items. It independently parses each
canonical reply body, validates unique IDs, item statuses/lengths and raw byte
SHA-256, and checks payload byte counts. The mixed body must contain two OK items
and one Unavailable item. All timeout, callback, stale binding, late reply,
reconnect, same-peer bootstrap and TX-release proof flags are required.

Verify a retained report or complete local runner evidence without hardware:

```sh
python -B tests/nucleo_h7s3l8/VerifyReadManyEvidence.py --report C:/external-evidence/read-many-report.json
python -B tests/nucleo_h7s3l8/VerifyReadManyEvidence.py --receipt C:/external-evidence/nucleo-read-many-unique-run
```

The latter also checks frozen source/executable identity, recorded command/log
hashes, and original/restored flash hashes. These checks establish consistency of
the retained evidence; invoking the verifier does not perform a new board run.

## What the runner verifies

- Snapshots the ELF and hashes the GUI, sources and tools. Requires an ARM ELF32
  little-endian executable. Every file-backed `PT_LOAD` physical address must lie
  inside `0x08000000..0x08010000`, beginning at the vector address. Every allocated,
  file-backed section must be covered by a validated LOAD. RAM `NOBITS` consumes
  no programming bytes. An independently converted binary is the only image
  passed to the programmer, with an explicit internal-flash address.
- Enumerates the selected probe with `-l stlink-only`; checks the COM USB parent;
  reads target identity through HOTPLUG before any reset/programming command.
- Reads all 65536 original flash bytes before loading the test image, checks the
  byte count and SHA-256, and preserves that backup in the new evidence directory.
- Programs and verifies the frozen test binary, then executes the headless
  `--serial COM6 --baud 115200 --check-device --report <external-JSON>` checker.
  Requires its fixed demo profile, at least three Fields and nine scalars, verified Voltage,
  exact U64 Counter and demonstration status, including the full decimal
  `18446744073709551615` string. After that process completes and closes its port,
  independently executes
  `--serial COM6 --baud 115200 --smoke --screenshot <external-file>`.
  Success requires exit zero, the actual QML endpoint/value delegate marker and
  a nonempty PNG. `-MinimumValueRows` defaults to three. These are runner acceptance
  minima; the current device checker validates all four Fields and seventeen scalars,
  including the nested Probe matrix and unknown signed enum. Use
  `-MinimumValueRows 4` for its root plus three Voltage children.
- Restores the original 64-KiB image in `finally`, up to three bounded attempts.
  Programmer verification alone is insufficient: a separate full flash readback
  must have the original SHA-256. A failed restoration makes the run fail and
  preserves the backup for recovery. The runner never launches another GUI after
  restoration begins.

`summary.json` records command argv, timing, owned process IDs, exit codes,
timeouts, stdout/stderr hashes, device/PnP identity, ELF load bounds, source/tool
hashes, screenshot and independent test/restore results. Command and GUI timeouts
are bounded; a timed-out invocation terminates only its own process, never all
instances of CubeProgrammer, Qt or ST-LINK tools.

Restoration covers the original internal flash bytes, not the original volatile
execution state. The test image must not write option bytes, external memory or
other persistent devices. Power loss, process termination or USB removal can
prevent any software `finally` block from completing; retain the original backup
until `restored_and_verified` is true.

## Serial lifecycle being qualified

The maintained serial profile uses existing COBS framing with CRC16 and a
2048-byte body, ordinary Resource LIST/STAT/READ and TEQ1 FieldRead. It adds no RPC
mailbox or alternate execution queue. One exclusive host submits sequentially
without retries. Server dispatch is synchronous and cannot pop another packet
while a previous reply, COBS TX lease or physical UART transmission remains.

A completely validated LIST → STAT → full Descriptor bootstrap establishes the
new remote ordering boundary. Clearing/closing QSerialPort or receiving an
uncorrelated LIST-shaped reply alone does not. Negative host tests must cover a
delayed old Resource reply, old TEQ1 reply, partial COBS frame, CRC failure, pending
reply, and DMA completion before UART physical completion. Descriptor parsing,
cursor/size/EOF checks and owner-thread invalidation remain required before a new
schema is admitted.
