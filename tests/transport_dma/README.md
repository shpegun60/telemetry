# UART DMA lifetime fixture

This directory prepares an application-owned STM32H753 UART DMA port. It changes
no Client, RPC, mailbox, FIFO, native model or wire API. The runner never opens a
UART, ST-LINK, network device or board and never flashes firmware.

The host executable uses the real bounded `DeviceClient`, `RpcBackend`, native
`RpcClient`, `RpcMailbox` and `RpcSessionPool<1>`. Its deterministic cases cover
cancel before permission, cancel after permission but before driver acceptance,
cancel/timeout/disconnect after DMA borrowing with and without server delivery,
retained bytes through abort, old RX after a new session, old software completion
cookies, and a lost final WRITE acknowledgement retried without duplicate endpoint
execution. These are simulated scheduling/loss cases, not electrical measurements.

The second executable compiles **the actual `H753Port.cpp`** against the deliberately
small `hal_double/stm32h7xx_hal.h`. It exercises TX/RX completion before pending abort
completion, foreign callbacks, queued RX events, UART errors, failed abort and
timeout while the driver still owns bytes. The double is not a Cube SDK or physical
DMA implementation. `Arm.cpp` is a portable Cortex-M7 compile fixture without HAL.
The optional separate H753 compile uses real CubeH7 HAL/CMSIS headers and records
their hashes; it still executes no MCU instructions.

## Run into external output directories

From the repository root, using a host C++20 compiler and Python 3.10 or later:

```sh
python3 -B tests/transport_dma/run.py --build-dir /absolute/external/dma-gcc --cxx g++ --arm-cxx arm-none-eabi-g++
python3 -B tests/transport_dma/run.py --build-dir /absolute/external/dma-sanitized --cxx clang++-18 --sanitize
```

For the real SDK compile, add `--hal-root /path/to/power_analyzer_h753` and
`--hal-cxx /path/to/arm-none-eabi-g++` to either command. `--hal-root` contains
`Core/Inc/stm32h7xx_hal_conf.h`, `Drivers/STM32H7xx_HAL_Driver/Inc` and the CMSIS
include trees. Windows also accepts the CubeIDE ARM compiler's full `.exe` path.
Use the STM32H753 application's actual configuration, not an unrelated board's
header. The test double include directory is never supplied to the actual SDK
compile. SDK UART/DMA enablement and `STM32H753xx` are required.

`summary.json` records commands, logs, tools, counted host results, source hashes
before/after, object/stack-usage hashes and allocation-symbol checks for compiled
ARM objects. The host scenarios are not a whole-program no-allocation proof.
Generated outputs remain outside this repository. Reusing an output directory
replaces that run's report; use a new directory to retain earlier evidence.

## Board integration contract

`H753Port` is a test fixture, not another generic DMA library. The application owns
UART selection, pin/clock/IRQ setup, framing, RTOS task and transport recovery.
Use one exclusive initialized UART with both `hdmatx` and `hdmarx` configured.
The constructor requires 8-bit/no-parity TX+RX UART, normal (not circular) DMA,
byte memory/peripheral alignment, disabled peripheral increment and enabled memory
increment, with TX memory-to-peripheral and RX peripheral-to-memory direction.
`validConfiguration(uart)` checks these without starting hardware. Do not reconfigure
the UART/DMA while the port is alive. Circular TX completion would not release its
buffer, so this fixture explicitly refuses it.
Client, backend, port methods other than the five `*Isr` methods run on one owner
thread. No callback directly calls Client from an interrupt. Forward the application's
existing HAL callbacks directly to the matching port:

| HAL callback | Fixture method |
| --- | --- |
| `HAL_UART_TxCpltCallback` | `txCompleteIsr(huart)` |
| `HAL_UART_AbortTransmitCpltCallback` | `txAbortCompleteIsr(huart)` |
| `HAL_UART_RxCpltCallback` | `rxCompleteIsr(huart)` |
| `HAL_UART_AbortReceiveCpltCallback` | `rxAbortCompleteIsr(huart)` |
| `HAL_UART_ErrorCallback` | `errorIsr(huart)` |

The fixture does not define these global callbacks, because the application may
already use them. ISR forwarding only publishes lock-free 32-bit events/cookies;
the owner consumes them in `poll(nowMs)`. Do not queue the forwarding itself into
another framework: old callbacks must be drained before a fresh HAL operation.

The owner loop calls `port.poll(nowMs)`, then obtains `client.nextPacket()` and
passes a nonempty packet to `port.send(packet)`. `send` copies into its 128-byte
test-owned TX storage **before** fresh `prepareHandoff`, then starts DMA and reports
complete-packet `onSendAccepted`. UART HAL acceptance is the transport handoff,
not peer receipt or endpoint completion. A failed or partially started send after
permission can have unknown outcome and requires recovery; it is never replayed
automatically in a fresh session. Normal same-transaction RPC retry remains native.

`receive(exactLength, session, requestId)` must be armed with the complete Resource
reply's validated framing length, at most 128 bytes. The application framing layer
must establish that length first; UART idle or a partial byte count alone is not a
complete packet. This fixture does not add COBS, CRC, length prefixes, UART_IDLE
parsing or CAN fragmentation. RX captures the original session/request when DMA
starts; a reply from that operation must never be relabelled to a later session.

TX storage remains immutable from successful DMA start until normal completion.
Once abort is requested it remains `AbortPending` until the **abort completion**
callback, even if a normal completion arrives first. RX uses the same rule.
`HAL_UART_ErrorCallback`, logical cancellation, timeout, `HAL_ERROR` and a return
from `HAL_UART_Abort*` do not establish release. A failed abort therefore leaves
the fixture quarantined. This follows the installed CubeH7
`stm32h7xx_hal_uart.c` abort contract: completion is reported by its abort callback;
the asynchronous abort return alone does not establish completion.

Software lease cookies reject already captured old completion events. They cannot
identify an arbitrary obsolete hardware interrupt arriving after a new HAL start:
the HAL callback supplies only a UART pointer. Correct reuse requires the HAL's
completed/aborted operation to be quiescent, pending UART/DMA IRQs drained under the
board's IRQ policy, and no delayed callback forwarding. This is an explicit board
integration precondition, not a generation-based guarantee about physical DMA.

## DMA memory and cache

Place the **whole port object** in statically allocated DMA-accessible SRAM, with
32-byte alignment. Its private TX and RX arrays each occupy four complete isolated
32-byte cache lines. A board linker section such as `.telemetry_dma` must map to
SRAM reachable by the selected DMA engine (for example suitable D2 SRAM), not
DTCM. Section naming alone does not make memory DMA-accessible. Keep its lifetime
longer than every possible HAL callback and every DMA operation.

Construction requires explicit `Memory` hooks. `cachedMemory(boardAccessible)`
selects CMSIS clean/clean-invalidate/invalidate operations and barriers for these
private full cache lines. `boardAccessible(pointer,128)` must validate the complete
range against the actual DMA-accessible linker region. The runner cannot infer
that physical mapping. For an MPU noncacheable region the application must instead
provide explicit appropriate hooks; do not select empty hooks for ordinary cached
SRAM. No hidden automatic cache-policy default exists.

`Policy` is a raw-byte **test admission policy**, not a generated device schema.
Before constructing/admitting the application Client, fill its expected header
from the intended peer's verified 64-byte descriptor identity. The host uses a
real local model descriptor. Admission is schema agreement, not peer authentication.
Use a generated `CodecPolicy` for an application SDK rather than copying this test's
mutable expected-header mechanism into a product.

Stop/join producers, close Client and keep processing until its callbacks drain.
Request port abort as needed, wait for real completion/abort callbacks, consume all
events and confirm `driverIdle()` before destroying/reusing its storage. Logical
Client draining alone does not establish driver idle. Close/drain the remote pool
binding and transport session before `confirmSessionClosed(oldGeneration)`, then
open a fresh session and admit its descriptor. Do not use admission as an abort or
server reset. No accepted request is silently replayed after reopening.

## Explicit H753 HIL steps (not executed here)

1. Select the board/UART/pins/baud/framing and transport in an application build.
   Record firmware/compiler/HAL/config/linker hashes and validate DMA memory/cache
   hooks before enabling requests. Flashing/running is a separate explicit action.
2. Trace TX start, DMA completion, abort request/completion, RX arm/completion,
   session, request ID, endpoint call count and result. Verify memory guards and
   unchanged borrowed TX bytes during cancel/timeout and delayed completion.
3. Cancel before and after handoff, while DMA remains active; inject completion
   before abort completion. Include UART errors and a failed abort path. Confirm
   that error/timeout never release or overwrite live DMA storage.
4. Drop the final WRITE acknowledgement and verify the same transaction's retry
   executes once. Disconnect/reset at request, execution and response phases;
   report `OutcomeUnknown` where execution cannot be established, close/drain the
   remote binding, and reopen without replay.
5. Delay RX and software ISR notification across session changes; ensure the old
   reply cannot complete a new request. Verify IRQ quiescence before rearming the
   same UART and capture physical framing/cache/DMA behavior under the board's
   representative electrical and scheduling load.

For later performance comparison, record enqueue-to-callback wall time (including
queue wait) and first-handoff-to-checked-result wall time separately. Use a monotonic
wrap-safe wall timer; DWT CPU-active cycles are a separate metric and must not be
presented as elapsed time while DMA/interrupts run. Retain Flash/RAM linker maps and
stack high-water evidence including interrupts. Compare with the same baud,
framing/cache/HAL configuration and the explicitly identified baseline (for example
`0ed24a9a24a4c28f4177037f7f8e5e67f74f6a44`). Suggested CSV fields are
`firmware_sha,config_sha,session,request,operation,payload_bytes,enqueue_us,handoff_us,checked_us,callback_us,cpu_cycles,tx_complete_us,abort_complete_us,endpoint_calls,status,stack_high_water`.

No existing host result or ARM compile receipt establishes that these board steps
have passed. Physical DMA timing, cache visibility, UART framing and electrical
behavior remain unmeasured by this fixture.
