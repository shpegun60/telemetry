# Optional structured protocol example

This is an application protocol example, not telemetry or resource core API.
It owns agreement state, correlation IDs and packet framing conventions.
Neither library requires it or includes it automatically. An application may
instead pass its own decoded u32 ID and payload directly to the existing
FieldIndex/CommandIndex/ServiceIndex encoded methods.

Descriptor/ValuesFile fingerprints are unchanged. Their comparison policy,
connections, admission, retries and disconnect handling belong to the caller.
The telemetry core contains only endpoint dispatch statuses; this example's
`PacketStatus` additionally describes invalid packets, version and readiness.


```cpp
#include <structured_protocol/Bind.hpp>
#include <structured_protocol/Exchange.hpp>

namespace rs = example::structured_protocol;
inline constexpr auto modelView = model.view();

// Per admitted connection; keep this and its Workspace in caller-owned storage.
rs::Binding peer;
auto agreement = rs::Bind::process(peer, modelView, descriptor.fingerprint(),
                                 completeBindRequest, bindResponseBuffer);
// Send only [0, agreement.written). Ready is published only with a full reply.

auto result = rs::Exchange::process(peer, completeRequest, responseBuffer, workspace);
// Send only [0, result.written); endpoint status is in the response header.
// On disconnect/reboot/model replacement, drain active work and old queues:
peer.reset();
```

`Binding` contains one borrowed pointer, with no fingerprint or peer table.
It starts Unbound and is neither copyable nor movable. The named `ModelView`,
its tables, names and binding sources must remain valid while Ready. The view
and supplied fingerprint must describe the same valid immutable model; do not
bind an invalid descriptor or change its model underneath an active peer.
Temporary views, braced temporaries and converting proxies are rejected by
the public Bind overload. As with other borrowed APIs, a helper can hide a
dangling reference; callers remain responsible for the actual lifetime.

Bind accepts exactly 16 bytes: `TSBN`, u16 major 3, u16 minor 0 and u64 cached
descriptor fingerprint, all little-endian. Its 8-byte response is `TSBA`,
u8 status and three zero reserved bytes. Status is Ready=0, SchemaMismatch=1,
UnsupportedVersion=2 or InvalidRequest=3. Every attempt clears an old binding;
failure cannot preserve Ready. A response buffer shorter than eight bytes
returns local BufferTooSmall with no bytes. Malformed requests with enough
reply capacity receive InvalidRequest. The local dispatch status for a schema
mismatch is NotReady. Bind supports overlapping input/output buffers.

This example defines 24-byte `TSRQ`/`TSRP` envelopes. Exchange request offsets:

| Offset | Bytes | Meaning |
| --- | --- | --- |
| 0 | 4 | `TSRQ` |
| 4, 6 | 2 each | major=3, minor=0 |
| 8, 12, 16 | 4 each | requestId, packed endpointId, payloadBytes |
| 20 | 1 | FieldWrite=1, Command=2, Service=3 |
| 21, 22 | 1, 2 | zero flags/reserved |
| 24 | payloadBytes | canonical native request |

The response uses `TSRP` and echoes version/correlation/endpoint/operation.
Its offsets 21/22/23 are dispatch status, endpoint status and zero flags.
Offset 16 is the actual response payload size. Only a successful non-void
Service carries a payload. Unknown application status codes become InternalError;
dispatch failures have endpointStatus=0 and payloadBytes=0. Lookup NotFound
and a callback's NotFound remain distinct. Likewise an empty target is dispatch
Unavailable, whereas a callback returning Unavailable is a valid endpoint result.

Exchange validates header/version/reserved fields, exact packet length,
Ready, O(1) packed-ID bounds, endpoint capability, native payload length,
response capacity and required scratch before invoking an application callback.
ReadOnly is returned before Field payload decoding. A successful request invokes
its target once. Invalid bool bytes, absent targets, insufficient output and
insufficient scratch never invoke it. Full potential Service response capacity
is required even if that call might return a failure status.

There is no hash, `sessionId`, TypeId or deduplication table in an Exchange
packet. The already bound view routes directly to existing encoded entries.
No lookup/hash code is added to the native Field/Command/Service paths.
Short requests, unknown magic or a response smaller than its 24-byte header
return a local error with `written=0`, without inventing request correlation.
Recognized header errors return the fixed response when it is safe to write it.

Input/output may overlap partially or completely: routing is captured and the
entire native request decoded before response writes. All-local endpoints
ignore Workspace. For other endpoints the complete request and the used
response prefix must not overlap scratch. If that overlap is rejected and
even the error header overlaps scratch, the result is local InvalidPayload
with `written=0`, preserving a caller's live scratch object. Successful calls
restore the incoming Workspace mark. These are individual operation guarantees,
not synchronization of application owners or of concurrent readers/writers.

The transport owns a bounded context for **every** admitted peer, including
Unbound peers. Bind/Exchange/reset on one context must be serialized. A context
and its Workspace cannot be reused while a handler is active. At capacity,
reject/defer admission; never transfer another peer's Ready. Disconnect/reboot
or model replacement clears agreement and old queued frames. Values READ is
also gated by the transport; discovery, LIST/STAT and descriptor READ remain
available before Bind. Resource providers and local encoded APIs themselves
do not acquire a session state. For datagram transports the lower layer must
exclude old connection generations; this is not inferred from a silent link.

`requestId` is a u32 correlation key, including zero and wrap. The client keeps
it unique among outstanding requests in one binding and also matches operation
and endpointId. Repeating a request executes it again; there is no automatic
retry, dedupe or rollback. A timeout must not immediately reuse its ID while a
late reply can arrive. A bounded pending table may retain the timed-out request
until its reply is consumed, or the connection is closed and old frames cleared.
Accepted commands manage their own application queue; Services are synchronous.

The [bounded transport fixture](../../tests/structured/exchange/FakeTransport.hpp)
demonstrates these integration responsibilities with two peers, separate
Workspaces, fixed queues and a bounded client correlation table. Its generation
is fake connection metadata, never a new telemetry wire field. A real UART/TCP
adapter supplies framing/reassembly and its own admission/disconnect mechanism.

## Explicit qmake selection

```qmake
include(path/to/lib/telemetry_structured/structured.pri)
include(path/to/examples/structured_protocol/protocol.pri)
```

Include `lib/resource/resource.pri` independently only when the application
also uses resource files/protocol. There are no reverse `.pri` includes.
This example uses protocol version 3.0; the version constants and status
mapping are owned here, independently of the descriptor implementation.
The preserved [oracle and regression suite](../../tests/structured/exchange/README.md)
checks packet bytes, borrowing, bounded fake transport and ABI GC/LTO paths.

## Dispatch wire codes

| Code | PacketStatus |
| --- | --- |
| 0 | Ok |
| 1 | InvalidRequest |
| 2 | UnsupportedVersion |
| 3 | NotReady |
| 4 | NotFound |
| 5 | InvalidPayload |
| 6 | BufferTooSmall |
| 7 | WorkspaceTooSmall |
| 8 | InternalError |
| 9 | Unavailable |

WriteResult, CommandResult and ServiceStatus are mapped by name in
[ExchangeWire.hpp](detail/ExchangeWire.hpp). A dispatch failure writes zero
endpoint status and no payload. Unknown statuses map to InternalError.
