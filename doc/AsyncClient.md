# Asynchronous generated clients

The optional client module provides `read`, `write`, `command`, and `service`
over the existing generated DTOs and standalone codecs. STM32 and Qt use the
same C++ `DeviceClient`; JS/TS use one TypeScript runtime with matching operation,
status, queue, deadline, and recovery contracts.

## One owner and one execution queue

One DeviceClient accepts several pending requests but activates one at a time.
The RPC backend exclusively owns one native RpcClient and one remote mailbox
session. Independent remote clients still use separate `RpcSessionPool<N>`
bindings; there is no server-global single-mailbox restriction.

Generate a C++ SDK, include `device_api/ClientBindings.hpp`, provide a backend,
and use owning `noexcept` callbacks:

```cpp
device_api::client::DeviceClient<8, 4096> client{backend, clock};
auto submitted = client.read(device_api::device::fields::Config,
    [](const auto& result) noexcept {
        if (result.hasValue()) consume(result.value());
    });
if (!submitted) report(submitted.status);
// Owner loop:
client.process(board_millis());
```

Admission takes the actual observed 64-byte Descriptor header and a fresh
nonzero session generation. The application first validates the full Descriptor
and binds it to its peer/model/session; the exact declared-header check is not
authentication or proof of the physical device model. Read-only Fields are
rejected before enqueue. Different endpoint categories cannot be interchanged.

FIFO refers to execution of published requests. Queued cancellation and elapsed
deadlines can complete while another request is active. Each accepted request
gets exactly one owner callback. Refused submissions return immediately and do
not register or invoke a callback. A callback may enqueue/cancel/close, but
reentrant `process()` does not run another callback. The active closure stays
alive until its invocation returns. Destruction of the client from a callback
is outside the lifecycle contract.

## Memory and results

`N` includes Reserved, Queued, Active and Completing slots. `PayloadBytes` is one
shared contiguous request-snapshot pool, not a maximum-sized buffer per request.
Fragmentation can return `PayloadFull` even when some disjoint bytes are free.
Snapshot encoding and owning callback construction occur before publication.
Callbacks are held in stable `DelegateSlot` storage with compile-time fit checks.

The generated policy sizes one aligned response DTO scratch by native
`sizeof/alignof`, independently of canonical wire extents. Native RPC request,
result, packet storage and port TX/RX copies are additional explicit buffers.
`Result::value()` and `ResultView::payload` borrow until callback return. Copy a
result before posting it to a different thread or a queued Qt invocation.

`ResultView::status` describes client/transport/decode completion; `dispatch`,
`endpointStatus`, and `codec` retain their separate meanings. Inspect success
before accessing a value. Command `accepted()` reports application admission;
`executed()` reports the endpoint's Executed status. Device motion, flash
durability and later application completion require application-owned evidence.

## Packet port and time

The port receives and sends complete packets. UART/TCP framing and CAN
fragmentation belong to that port. All owner clock reads and `process(now)` use
one modulo-u32 millisecond domain; intervals are in `1..0x7fffffff`. Deadlines
start at submission, including queue wait. Service the owner at least once in
each half clock range; arbitrary multi-wrap pauses cannot be disambiguated.

Copy `client.nextPacket()` bytes into bounded port-owned storage, then call
`client.prepareHandoff(session, requestId)` immediately before the first possible
external transfer. It rechecks cancellation, session and the overall deadline.
False forbids send. Report `client.onSendAccepted()` only after the transport
accepts the complete packet. Partial writes do not qualify. The port's copy
remains alive and unchanged until the driver releases it, including after a
client timeout, reset or disconnect. TX release and endpoint completion are
independent events. Deliver complete replies through `client.onReply()`.

## Backends and recovery

Encoded request/reply v1 adds an independent 32-byte LE envelope; it changes
neither Descriptor v3 nor RPC v1. Every operation receives a status response.
The server uses [EncodedDispatcher](../lib/resource/telemetry/v3/EncodedDispatcher.hpp)
to call existing Model Encoded operations. Session, request, operation, endpoint,
lengths and statuses are checked. It has no retry or server deduplication;
delivering the same request twice may execute it twice.

RPC wraps the moved existing engine, retaining byte-identical same-transaction
retries, polling and ACK gating. No backend automatically replays an operation
across sessions. After possible execution, lost completion of a mutation yields
`OutcomeUnknown`; reads can report Timeout/Cancelled with a separate recovery
gate. In RPC even a read uses final WRITE and can leave a retained server slot.
Unsent queued requests are cancelled when the session is invalidated.

Recovery requires actual closure/drain of the old physical and remote session,
then `confirmSessionClosed(oldGeneration)`, then Descriptor admission for a fresh
generation. Calling reset or admitting the header does not prove remote closure.
Cancelling an active mutation does not undo a possible remote effect.

## Multiple producers and shutdown

Instantiate `DeviceClient<..., Lock>` with a task mutex policy and wrap it in
`ThreadSafeClient`. Producers reserve a stable record, initialize privately, and
publish under a short lock. Only successfully published requests are accepted;
FIFO follows publication order. No I/O, encoding, closure destruction or user
callback executes while the queue mutex is held. The wrapper wakes the owner
after publication/cancellation/close. A handle identifies owner, session and a
nonreused sequence; handles are valid only during that client instance's life.

Shutdown is explicit:

1. `close()` stops new admission.
2. Let in-progress producer calls return and stop/join those producers.
3. Continue the owner loop. It cancels/completes accepted work and waits for
   private producer reservations to be released without blocking.
4. Wait for `drained()`, then stop/join the owner and close/drain the transport
   and remote session before destroying their storage.

An already stopped owner cannot deliver owner-thread callbacks. The destructor
does not manufacture a drain. The mutex facade is not an ISR interface. A board
port separately owns ISR/main-loop publication, DMA-accessible storage and cache
maintenance.

## Platform adapters and qualification

Generated `DeviceClientAdapter.hpp` keeps QML tokens/pending bookkeeping and
submits to the sole core execution queue. Queue capacity failures are explicit
`QueueFull`/`PayloadFull` results, with no second hidden execution queue. Use one
adapter per DeviceApi. Both RpcBackend and EncodedBackend are supported.

For JS/TS add `--client-runtime` to the normal generator command; codec modules
remain usable independently. Promise results follow the same statuses, with
`cancel()` on each returned Promise. C++ uses fixed-width integers, JS uses
`bigint`, and existing QML conversion rules keep wide integers as decimal text.

The [bare-metal example](../examples/async_client/README.md),
[C++ qualification](../tests/async_client/README.md), and
[web qualification](../tests/client_web/README.md) exercise the implementation.
The CI jobs retain generated outputs and logs. Host/Qt/sanitizer runs and ARM
compiler stack evidence do not establish electrical disconnect/DMA behavior on
an untested physical board.
