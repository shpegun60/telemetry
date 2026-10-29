# NUCLEO-H7S3L8 Inspector device over ST-LINK VCP

This is a standalone, maintained demonstration for **STM32H7S3L8H6** on
NUCLEO-H7S3L8. It has no Ethernet, Qt, RTOS, fiber scheduler or RPC mailbox.
The `while (1)` owner composes the existing external COBS UART DMA driver,
COBS fixed pools, Telemetry Model, Resource provider and Encoded dispatcher.
The example does not modify the instrument firmware or either existing H7S
bench project. Published voltages are demonstration constants, not ADC readings.

USART3 uses **PD8 TX / PD9 RX**, GPDMA1 channel **10 TX / 11 RX**, 115200 baud,
8 data bits, no parity, one stop bit and no flow control. These pins connect to
the board's ST-LINK VCP under its standard solder-bridge routing. The driver uses
normal ReceiveToIdle DMA with chunk switching, not a second DMA implementation.
UART and RX DMA share preemption priority 5. HSE is 24 MHz; the configured core
clock is 600 MHz, AXI 300 MHz and APB1 150 MHz, matching the local H7S bench.

## Wire profile and ownership

The shared [SerialProfile.hpp](../inspector_serial/SerialProfile.hpp) selects
`cobs::Format<crc::Crc16Bitwise, 2048>`: a two-byte little-endian body length,
ordinary payload, existing CRC16 and COBS delimiter. Each decoded body is one
unchanged Resource request/reply or TEQ1 request/reply. There is no route byte,
TCP stream framing, RPC header, new wire operation or automatic resend.

The supported profile is one exclusive, sequential host: one outstanding
exchange and no later bytes from a retired producer after fresh bootstrap starts.
The device accepts only Resource LIST/STAT/READ and TEQ1 FieldRead. Resource
WRITE and TEQ1 FieldWrite/Command/Service are dropped before provider/Model dispatch.
The Model itself has no Commands or Services and all Fields are read-only.

[ServerLoop.hpp](ServerLoop.hpp) pops at most one complete COBS packet per step.
It does not dispatch while any of these remain: the UART's physical TX borrow,
the COBS endpoint's unreclaimed TX lease, or its single pending reply. A fresh
LIST therefore cannot overtake old published RX work or old UART transmission.
Providers execute synchronously; there is no deferred work or execution queue.
The server has no pinned TCP-style session: a valid read uses the current
request's nonzero TEQ1 session, and the host correlates its response.

A fresh **validated bootstrap transcript**, including LIST, can establish an
old-work drain boundary only under that restricted host/server profile. Closing
a COM port or clearing an OS buffer does not prove remote closure; Resource
replies have no transaction identity. The host's recovery/admission policy must
retain that distinction. This example does not promise reliable mutation or
exactly-once execution across reconnect.

The entire Uart object and COBS pool live in AXI SRAM `.bss` at `0x24000000`,
accessible to GPDMA. I/D caches are enabled. RX chunks fill aligned 32-byte cache
lines; the existing driver invalidates RX and cleans TX. DMA storage never lives
in DTCM. The COBS endpoint owns transmitted encoded bytes until the driver reports
physical release, normally UART TC after the last stop bit, including error
recovery. An exhausted DMA counter alone does not release the lease. Main owns
parsing/dispatch; interrupt callbacks only drive the existing UART state machine.

## Descriptor and demonstration fields

LIST discovers `/telemetry/descriptor.bin` after a version-file decoy; Descriptor
is deliberately file index 1. Clients must discover its index and read its full
size using STAT and cursor READ. Descriptor v3 and TEQ1 bytes are unchanged.

| Field | Endpoint ID | Value |
| --- | ---: | --- |
| `measure.Voltage` | 0 | Struct: Ua 230.1, Ub 229.8, Uc 231.2 |
| `device.Counter` | 65536 | U64 `18446744073709551615` |
| `device.Status` | 65537 | U32 uptimeMs, requests, fieldReads, refused; Bool demonstration=true |
| `device.Probe` | 65538 | Matrix `[[1,-2,3],[4,5,-6]]`, Enabled=true, signed open enum mode=-2 |

The enum declares Idle=-7 and Active=12; -2 must display as an unknown signed code.
Status is captured by the single owner at dispatch. Its counter values include
that Status request and are diagnostics, not metrology quality information.
The exported volatile `telemetry_h7s_diagnostics` symbol gives SWD-readable magic,
loop/packet/reply/refusal counts, TX failures and UART overrun/error/restart counts.
There is no text console mixed into the protocol stream.

## Offline build

Dependencies remain external and retain their [original licenses](LICENSES.md):

- a COBS checkout at `2e0abf260848fcb74e4a57b37532188047759643`, with its
  `libs/spsc` submodule initialized;
- STM32CubeH7RS, qualified here with `STM32Cube_FW_H7RS_V1.3.0`;
- STM32CubeIDE's ARM GNU compiler; CMake and Ninja.

Prepare the external COBS dependency once; only its SPSC submodule is needed:

```powershell
git clone https://github.com/shpegun60/cobs C:\path\to\COBS
git -C C:\path\to\COBS checkout 2e0abf260848fcb74e4a57b37532188047759643
git -C C:\path\to\COBS submodule update --init libs/spsc
```

This example uses Telemetry's included `tiny_delegate.hpp`. The COBS delegate,
STM32F1 and STM32G4 submodules are not dependencies of this firmware.

From the Telemetry root, use a fresh output directory outside all source trees:

```powershell
examples\nucleo_h7s3l8\Build.ps1 `
  -CobsRoot C:\path\to\COBS `
  -OutputDirectory C:\artifacts\telemetry-h7s-firmware
```

`-Stm32CubeRoot`, `-ArmGccBin`, `-CMake`, `-Ninja` and `-Jobs` are configurable.
The script finds CMake and Ninja on `PATH` first, then checks the conventional
Qt Tools locations. Explicit tool overrides accept executable paths or command
names, such as `-CMake cmake -Ninja ninja`; a Qt installation is not required.
The script resolves the installed CubeIDE compiler and rejects CubeCLT. It builds
C++20 with `-Os` and LTO against SDK sources in place. The internal 64 KiB Flash
linker region is enforced; no external-flash loader or RAM-run fallback is used.
AXI SRAM holds data; DTCM holds the main stack. The SDK startup initializes static
storage and constructors before `main`; the application never exits.

The external receipt contains ELF/BIN/MAP, LTO `.su` stack reports, exact build
commands, compiler identity, tool hashes and before/after source hashes. A linked
allocator symbol fails qualification. `.su` rows describe individual compiler
frames, not measured maximum stack including nested interrupts.
This script never opens a serial port, invokes ST-LINK or programs a board.

## Host contract checks

```powershell
python -B examples/nucleo_h7s3l8/tests/run.py `
  --cobs-root C:\path\to\COBS `
  --build-dir C:\artifacts\telemetry-h7s-host `
  --cxx C:\Qt\Tools\mingw1310_64\bin\g++.exe `
  --cmake C:\Qt\Tools\CMake_64\bin\cmake.exe `
  --ninja C:\Qt\Tools\Ninja\ninja.exe
```

Linux may use `--cxx clang++-18 --sanitize`. These checks compile the **actual**
Protocol and ServerLoop. Real COBS fixed pools encode/decode both directions;
allocation traps reject hidden C++ heap use. They hold UART busy, then separately
hold the COBS lease, proving a queued LIST cannot bypass either. An old read is
completed before fresh LIST. Other cases verify Descriptor discovery/bytes,
refusal of every mutation operation, corrupt CRC, truncated-frame gap recovery,
new sessions and no automatic retry after TX-start refusal.

Hardware qualification is separate: verify board identity and backup first,
program only under explicit authorization, use the actual VCP device name,
then check Descriptor/fields, reconnect, corrupted frames, DMA/cache diagnostics
and lifetime. Neither an offline link nor a host test proves the physical UART,
ST-LINK USB path, cache coherency or stack/CPU budgets on a connected board.
