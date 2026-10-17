# H7S RPC mailbox UART integration

[Recorded results](RESULTS.md) retain measured hardware coverage separately
from this fixture contract.

This fixture sends unchanged Resource packets over the NUCLEO-H7S3L8 USART3
virtual COM port at 115200 baud. Its owner admits one peer, dispatches a
Pending mailbox only after the complete successful WRITE ACK transfer, and
uses the real HAL tick for the 10-second Receiving timeout. It exercises
compiled RpcMailbox, Resource Protocol, Model Adapter and structured ABI
boundaries. The Model has one Service and one Command.

The UART envelope and its session/hold controls belong only to this test
fixture. They are not additions to the [RPC wire contract](../../../../doc/RpcMailboxV1.md).
The session control simulates an application disconnect and drained new
session. UART has no TCP disconnect event: these checks establish neither
Ethernet behavior nor physical cable-removal recovery.

## Offline build and verifier

Use GNU Arm 14.3.1 shipped with STM32CubeIDE and the existing H7S Cube
scaffold, containing `Boot/Core` and `Drivers`. Supply a fresh output directory:

```powershell
python tests/structured/rpc/h7s/run.py --cube <scaffold> --arm-cxx <arm-none-eabi-g++.exe> --output <fresh-output>
python tests/structured/rpc/h7s/verify.py --self-test
```

Offline build mode compiles and links both `-O2` and `-Os` images, checks every
loadable section and binary against the complete 64 KiB internal Flash region,
and records captured library/fixture bytes, Cube inputs, compiler identity,
flags, objects, disassembly, section sizes and individual stack frames. It
does not import a serial package or access a probe or UART port. The hermetic
verifier uses synthetic receipts for its rejection controls and produces no
hardware evidence.

## Explicit hardware run

Only the coordinating root agent accesses the explicitly selected board. With
that board authorized, append all four options to the build command:

```powershell
python tests/structured/rpc/h7s/run.py --cube <scaffold> --arm-cxx <arm-none-eabi-g++.exe> --output <fresh-output> --run --programmer <STM32_Programmer_CLI.exe> --serial <adapter-serial> --port <COM-port>
```

Both images are built and validated before target access. The runner verifies
the board identity and selected serial, reads the complete 64 KiB internal
Flash before programming, and restores that backup in `finally`. A fresh full
readback must match the backup hash. `receipt.json` records partial failures
as well as completion and restoration. Each optimization receives its own
computed check totals and exact request/reply bytes in `O2-uart.jsonl` or
`Os-uart.jsonl`. PySerial is needed only for `--run`.

```powershell
python tests/structured/rpc/h7s/verify.py --receipt <output>/receipt.json --self-test
```

Receipt validation checks recorded provenance and coverage internally. It
does not compare historical input hashes against later working-tree bytes.
Publication identity must be checked separately against the captured inputs.

## Transport and checks

An envelope is `u16le bodyBytes`, followed by `u8 kind`, `u32le peer`,
`u32le generation`, and the payload. Length is checked before reading into
fixed 256-byte frame storage. Four complete frames can be queued. Prefix,
body, drain and transmit waits are finite. A partial UART envelope times out
and is drained before the next complete frame. The benchmark has two files:

| File index | Path | Purpose |
| --- | --- | --- |
| 0 | `/telemetry/rpc.bin` | RpcMailbox Resource provider |
| 1 | `/fixture/stats.bin` | Read-only 44-byte owner counters |

Kind 1 carries a Resource request/reply. Kind 2 carries one byte to hold/release
Pending execution. Kind 3 with empty payload accepts a strictly newer
application generation, resets the mailbox and drains its queue. Peer 1 is
admitted; kind 127 replies with refusal reason 1 for foreign peers or 2 for
stale generations. These fields are outside the Resource packet.

The Service consumes a 68-byte encoded request and returns a 200-byte encoded
result, so its 232-byte RPC record requires continuation reads. The Command
has no arguments and no result. Both endpoints have packed ID zero in their
respective endpoint families. The independent stats words are marker
`0x53504352`, clock Hz, Service calls, accumulated value, callbacks before ACK,
generation, admission refusals, Resource packets, completed WRITE ACKs,
queue drops and Command calls, all `u32le`.

Each image must meet these scenario coverage minima; receipt totals remain
the actual number of conditions evaluated:

| Scenario | Minimum checks | Behavior |
| --- | ---: | --- |
| UART split | 4 | Split prefix/packet, incomplete-envelope recovery |
| Fragment retry | 4 | Fragmented WRITE, discarded final ACK, identical Pending retry |
| Pending | 3 | Header-only snapshot, callback held |
| Ready | 7 | Paged result, immutable repeated reads, Ready WRITE retry, one Service callback |
| Generation | 6 | Pending cancellation, stale/foreign refusal, IDs restart after session reset |
| Timeout | 5 | Actual 10-second HAL timeout, retained expired ID, newer request accepted |
| Command | 6 | Discarded final ACK, Pending/Ready retry, one callback |
| Stats | 3 | Read-only file, 600 MHz clock, ACK ordering and bounded queue counters |

The verifier rejects missing groups, reduced coverage, nonzero failures,
inconsistent totals/provenance, unbounded images and incomplete restoration.
