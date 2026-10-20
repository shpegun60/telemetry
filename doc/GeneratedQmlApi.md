# Generated Qt/QML API

Generate the Qt backend with `--targets qml`. It reuses the existing generated
C++ DTOs, endpoint constants and standalone codec without changing a device's
Model or the wire formats. The main generated module needs C++20, Qt Core and
Qt Qml. The standalone `cpp` target retains its existing dependencies.

The default namespace produces `device_api::qml::DeviceApi` and the QML import
`Telemetry.Generated.device_api 1.0`. `DeviceApi` is an instantiable type;
applications may also expose their own instance as a context property. Group
objects are owned by that instance. The generated `DeviceApi.hpp` declares the
actual normalized group, property and method names for each descriptor.

Outputs under `qml/` include `DeviceApi.hpp`, `DeviceApi.cpp`, `QmlValues.hpp`,
`QmlInput.hpp`, `CMakeLists.txt` and the optional `QmlRpcAdapter.hpp`.
The module's CMake target exports its include directories. Scoped macro shields
preserve otherwise valid DTO identifiers such as `signals` and `Q_OBJECT`,
then restore the application's Qt macros. Existing Qt `signals`, `slots` and
`emit` usage remains available. Its QML URI follows the chosen namespace.
Choose a host namespace such as `device_api` or `company::device_api`; Qt-style
`Q...`/`Qt...` components, Qt integer aliases and Qt macro names are refused
for this target, since moc also uses the requested namespace. Descriptor member,
endpoint and group names are normalized or shielded independently.
Keep generated SDKs and build directories outside the
source repository, as shown by the [Qt example](../examples/codegen_qml/README.md).

Field getters return confirmed local snapshots. Reading a property never sends
a packet. A readable Field has `refresh<Name>()`; a writable Field has
`write<Name>(value)`. Both return an opaque decimal `QString` request token
immediately. Commands and Services similarly return tokens from their generated
methods. Every operation completes later through `<name>Finished(token,
success, accepted, value, errorKind, message)` and the device-wide
`operationFinished(token, result)` signal.

```qml
import QtQuick
import Telemetry.Generated.device_api 1.0

Item {
    DeviceApi { id: backend }
    Text {
        text: backend.device.configValid
            ? backend.device.config.serial : "No confirmed value"
    }
    // The application admits the peer and attaches a transport first.
    // A button can then call backend.device.refreshConfig().
}
```

Each Field also has `<name>Valid`. It starts false, becomes true after a
successful read and becomes false when the session ends. A successful write
invalidates an existing cached value; refresh the Field to obtain the owner's
actual value. A setter may normalize a request, so the submitted value is not
silently installed as a confirmed snapshot. Failed reads and writes do not
replace an existing confirmed value.
After a Command or Service that may change a Field, refresh that Field explicitly.
The descriptor does not declare those dependencies; validity describes the last
confirmed snapshot in this session, not continuous synchronization with its owner.

The result map contains `success`, `accepted`, `value`, `errorKind`, `message`,
`dispatchStatus`, `endpointStatus`, `operation` and `id`. Success means dispatch
and application status zero plus a valid response of the expected length.
An Accepted Command reports `accepted=true` and `success=false`, with no error;
the owner queued work and no eventual physical completion is established.
The operation family remains available because the same endpoint status byte
has different meanings for Fields, Commands and Services. A refused Service
with an empty response does not become a successful empty object. A void
response and a zero-byte empty struct remain distinct in the generated API.

Structs use `QVariantMap` values with the descriptor's exact member names;
arrays use `QVariantList` with the declared fixed length. Unknown and missing
struct keys, wrong array lengths and invalid scalar values are refused before
transport dispatch. Bool accepts a boolean, not a truthy substitute. Integer
values must fit their wire type without fractional conversion. Enum values
use their underlying integer representation; dictionary labels do not close
the accepted numeric domain.

Every U64 and S64 value, including a 64-bit enum code, uses a canonical decimal
`QString` in both directions. Examples are `"18446744073709551615"` and
`"-9223372036854775808"`. Numbers, whitespace, a plus sign, leading zeros and
out-of-range strings are refused. Keep these values as strings in QML;
conversion through a JavaScript Number can lose precision. Request tokens
and the schema fingerprint are strings for the same reason.

JavaScript object and array inputs are copied into bounded native snapshots
before DTO conversion. A QML-created `DeviceApi` obtains its engine through
Qt's association. A C++-created instance that receives JavaScript objects
must use `setInputEngine(QJSEngine*)` with their originating engine. The engine
and input access must use the device object's owner thread. Native
`QVariantMap` and `QVariantList` inputs need no engine. Cycles, unsupported
objects, throwing accessors, sparse arrays, extra own string keys and exceeded
depth/node/collection budgets produce a validation error. Snapshotting does
not retain a live JavaScript object as a pending wire request.

Before requesting an endpoint, call `admitDescriptor(header)` with exactly the
peer's 64-byte Descriptor v3 header. It checks the generated header identity,
including the declared fingerprint, version, flags, counts and offsets. It
does not hash the remote body or authenticate the peer. Obtain the header
through the application's existing peer/session admission process. Passing a
complete descriptor file instead of its header is refused.

Each admission attempt ends the prior session, increments its generation and
invalidates cached values and pending requests. `ready()` becomes true only
after a matching header. `resetSession()` ends admission explicitly. Generations
and request tokens never wrap into an old request's namespace. A completion
must carry the original token and session; stale and repeated completions are
ignored. The API permits up to 64 pending requests and one pending operation
per endpoint family/ID; Field read and write share that pending slot.

Once `complete()` validates a reply on the owner thread, its result is fixed.
A reset before the queued completion signals does not change that result or its
decoded value. An adapter's completion handler calls `complete()` directly on
that thread once its checked result is available; completion signals remain queued.
Field cache updates still require the original session to be
current and ready: an old read cannot populate a new session's cache, and an
old write cannot invalidate it. A worker-thread completion queued before reset
but validated afterward remains stale and is ignored.

The application transport receives:

```cpp
void requestReady(QString token, quint32 session, int operation, quint32 id,
                  quint32 requestType, quint32 responseType, QByteArray payload);
```

| Operation | Value | Request TypeId | Response TypeId |
| --- | ---: | --- | --- |
| FieldWrite | 1 | Field value | 0 |
| Command | 2 | Declared request or 0 | 0 |
| Service | 3 | Declared request or 0 | Declared response or 0 |
| FieldRead | 4 | 0 | Field value |

The transport preserves operation, ID, token and session while routing the
owned payload. It reports either
`complete(token, session, dispatchStatus, endpointStatus, payload)` or
`fail(token, session, kind, message)`. `complete` and `fail` can queue a
worker-thread result back to the API; never access its group properties from
another thread. A local transport must check `requestActive(token, session)`
on the API's owner thread before starting a queued request. For worker-owned
dispatch, carry the owned job and its session/cancellation policy across that
boundary. A timeout after handoff does not undo a callback or a device action.

By default, emitting `requestReady` conservatively means a mutation might execute.
A transport with exclusive ownership can opt into precise handoff tracking using
`setTransportHandoffTracking(true)` before any request is pending. Every consumer
of `requestReady` must then comply: call
`transportHandoffAccepted(token, session)` on the owner thread immediately before
the first action that permits execution, such as invoking a native callback,
enqueuing an irrevocable worker job, or handing bytes to the transport. It is a
one-shot gate: if it returns false, do not execute or send the request. Marking
handoff after sending, or having a second legacy sender on the same API, breaks
this guarantee. The setter refuses changes while requests are pending.

`requestTimeoutMs` defaults to 5000 and accepts 1 through 300000. Local input,
admission and capacity failures finish asynchronously without sending a
request. Expired reads report Timeout. In precise mode an expired mutation that
never passed its handoff gate also reports Timeout, and reset cancels it.
An already handed-off mutation can report OutcomeUnknown because its effect may
have occurred; legacy mode makes that conservative assumption at `requestReady`.
Session loss and cancellation preserve this distinction. `fail(kind, message)`
trusts the transport's evidence: do not claim cancellation or nonexecution after
a mutation may have executed. No automatic operation retry or
replay occurs across sessions; the application decides how to reconcile an
unknown outcome.

The optional `DeviceClientAdapter.hpp` connects `DeviceApi` to the common
bounded [DeviceClient](AsyncClient.md) execution queue. Its constructor takes
`(api, backend, clock)`; select `RpcBackend(nativeRpcClient, clock)` or
`EncodedBackend(txStorage, resultStorage, clock)`, or the optional
`RetryingEncodedBackend(txStorage, resultStorage, clock, EncodedRetryOptions{100,3})`
from `integration/encoded/RetryingBackend.hpp`. The retry choice uses the same
adapter and one execution queue; it may execute a delivered operation repeatedly
and requires a fresh permission and released TX buffer for each retry.
API, adapter, backend and
borrowed buffers use one owner thread. `DeviceClientAdapter<Api, N, PayloadBytes>`
defaults to eight records and one shared 4096-byte payload pool. Its token map
is pending/completed correlation bookkeeping, not a second execution FIFO.
Queue and payload exhaustion explicitly report `QueueFull` and `PayloadFull`.
Do not also attach the legacy RPC adapter or another requestReady sender.

Drive `process(nowMs)`, obtain `nextPacket()`, copy its bytes into bounded
port-owned storage, and call `prepareHandoff(packet.session, packet.requestId)`
before the port can send. A false result forbids the transfer. Report
`onSendAccepted(session, requestId)` after complete packet handoff and deliver
the paired reply through `onReply(bytes, session, requestId)`. The adapter gates
the QML operation's handoff only when `packet.mayExecute` is true: a complete
Encoded request or the final RPC WRITE. Repeated RPC packets retain the same
transaction and one-shot permission. Every gate uses a fresh clock/deadline;
an expired unsent request reports Timeout, independently of Qt timer ordering.
Transport acceptance, TX/DMA buffer release and endpoint completion remain
separate events. The backend may reuse its own outgoing storage after timeout;
the port must retain its copy until the driver releases it.

Session recovery is explicit for both backends. Stop old traffic, drain/exclude
port copies and close the actual remote session before
`confirmSessionClosed(recoveryGeneration())`, then admit a newly observed exact
header. For RPC, release/drain the old pool handle and acquire a fresh connection;
local API admission cannot reset its server state. Read failures may retain
Timeout while still requiring remote session recovery. No queued invocation
is replayed after recovery. On shutdown stop producers, call `close()`, and keep
owner processing until `drained()` before destroying the adapter. This lifetime
precondition keeps accepted callbacks alive through their single completion.
The [runnable example](../examples/codegen_qml/README.md) uses this same adapter
and FIFO for both actual Encoded and RPC loopbacks.

Including `QmlRpcAdapter.hpp` instead adds the legacy single-active optional
`RpcAdapter<Api=DeviceApi>`. It borrows the existing `RpcClient` exclusively;
the client and its caller-owned request, response and packet buffers must
outlive the adapter. API, adapter and client use one owner thread. This adapter
performs no blocking I/O. The application drives `process(nowMs)` from an event
loop and exchanges complete packets through `nextPacket()`,
`prepareHandoff(generation, requestId)`, `onSendAccepted(generation, requestId)` and
`onReply(bytes, generation, requestId)`. Report complete packet handoff before
processing another UI/session event, and preserve the packet's generation and
request ID when delivering its reply. Framing, partial socket writes and
received-frame assembly remain the transport's responsibility.

Use `adapter.enableHandoffTracking()` before requests to enable the precise
mode for this transport. For each offered packet, call `prepareHandoff` before
the transport accepts any bytes; false means the packet must not be sent.
Only the final Resource WRITE can seal the RPC request, so that packet passes
the API's one-shot execution gate. Identical retries of that invocation retain
the gate; a new invocation starts with a fresh gate. Call `onSendAccepted` only
after complete packet handoff. The adapter keeps the pre-handoff evidence even
if a deadline or reset occurs before that callback. Calling `onSendAccepted`
without the required preparation in precise mode closes local admission and
requires recovery; it cannot undo bytes already sent or repair a prior false
timeout result. The default adapter retains legacy conservative behavior.

The adapter maps generated operation/ID/type metadata to the existing typed
`beginReadField`, `beginWriteField`, `beginCommand` and `beginService` methods.
It decodes the standalone request bytes into the exact generated DTO and uses
`RpcBindings.hpp`; RpcClient continues to use its existing native Codec.
This legacy adapter uses typed RpcClient entry points. Responses are copied before
queued completion and pass the API's status, length and DTO validation.
Its single active invocation means additional concurrent requests receive a
Busy transport result. Existing RpcClient packet retries remain within the
same transaction and generation.

`beginSession()` synchronizes the already admitted API generation.
`loseSession(generation)` preserves the client's terminal diagnostic, queues
any pending failure and then invalidates that API session. Destruction does
the same while the borrowed client still exists. Expired or cancelled API
requests cannot start later from an old queued signal; active requests are
stopped at packet/event boundaries before another handoff. The adapter never
replays an OutcomeUnknown invocation on a new peer.

Ending an existing native session, abandoning a prepared final WRITE, or a
native OutcomeUnknown latches `recoveryRequired()`. `recoveryGeneration()`
identifies that adapter-local generation. Packet/begin APIs remain blocked;
admitting another header alone cannot clear the latch. The application must
exclude old frames and close/drain the actual remote session, then call
`confirmSessionClosed(oldGeneration)` and admit a newly obtained matching
header. Confirmation returns false for the wrong generation, only removes the
local gate, and never replays work or asserts that a prior effect was undone.

For `RpcSessionPool`, release the old handle and wait for a Running callback's
Closing state to drain before acquiring a fresh connection handle. Calling
`acquire` for the same active key returns Existing and does not clear Pending.
The exact final WRITE reply must have been handed off before `replyAccepted`;
without that acknowledgement the pool deliberately holds Pending and does not
dispatch it. A local RpcClient generation change does not release the server
handle or reset its transaction high-water mark. Even a read request uses an
RPC WRITE envelope, so a read timeout can require session recovery while its
UI result remains Timeout. Pool handle generations and API/client generations
are separate identities.

Construct an adapter with a fresh client, or with a known closed remote session
and coordinated native generation history. Retained non-idle client state
causes a conservative recovery latch with generation zero, which must be
confirmed explicitly. A caller can erase native evidence by resetting the
client to Idle; the adapter cannot inspect its private session/generation state
afterward. Recreating an adapter or client never substitutes for closing the
remote session. Recreating the API also restarts its epoch counter, so use a
fresh client unless the application coordinates a strictly newer epoch.

A direct Encoded transport can instead use the existing native
`readFieldEncoded`, `writeFieldEncoded`, `executeCommandEncoded` and
`callServiceEncoded` functions. It supplies its own disjoint wire buffers and
Workspace, preserves the returned dispatch/application status and `written`
count, and reports the result through the same token/session completion
boundary. It does not need RpcClient, RpcMailbox, Resource framing or retry
machinery. Serialize Model/owner/Workspace access and keep slow native
callbacks off the UI thread.

See the [Qt example and build commands](../examples/codegen_qml/README.md),
[QML regression runner](../tests/codegen_qml/README.md),
[standalone C++ API](GeneratedCppApi.md) and
[schema compatibility contract](GeneratedSchemaCompatibility.md).

The relevant Qt contracts are documented in
[C++/QML data conversion](https://doc.qt.io/qt-6.10/qtqml-cppintegration-data.html),
[the JavaScript host environment](https://doc.qt.io/qt-6.10/qtqml-javascript-hostenvironment.html),
[QJSManagedValue exception handling](https://doc.qt.io/qt-6/qjsmanagedvalue.html) and
[qt_add_qml_module](https://doc.qt.io/qt-6/qt-add-qml-module.html).
The qualification suite executes the installed runtime rather than assuming
that its JavaScript features match Node or a browser.
