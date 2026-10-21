# DynamicClient implementation contract

This document specifies the first C++ DynamicClient implementation. It describes
planned behavior, not an available API or a completed qualification. Writer
acceptance remains separate from this work. The implementation belongs in its
own PR after that acceptance.

## Responsibilities

The optional Integration module binds [RuntimeSchema](RuntimeSchema.md) to the
existing [DeviceClient](AsyncClient.md). Runtime describes, reads and edits wire
values; Client owns scheduling, deadlines, cancellation and completion;
Integration checks runtime endpoint contracts and interprets response values.

DynamicClient privately owns its core. It uses the existing `submitEncoded()`
FIFO and existing Encoded, Retrying Encoded or RPC backend. It adds no execution
queue, transport, dispatcher or wire format. Application code cannot obtain the
core to bypass runtime checks through raw submissions or static typed calls.
Client core must not include RuntimeSchema or depend on Runtime, Resource or Qt.

The default static admission policy retains the exact 64-byte
`Codec::descriptorHeader()` comparison and existing typed endpoint checks.
Opting into runtime admission must not admit schema A while leaving typed calls
compiled for schema B usable on the same instance.

## Admission and endpoint binding

One client has one immutable admitted schema/session binding. Admission requires
a successfully parsed full Descriptor, its exact header matching the observed
peer, and an admissible transport session. Fingerprint comparison is descriptor
identity checking, not peer authentication.

The binding retains workspace identity, schema generation, descriptor identity
and admitted session. Endpoint bindings additionally retain category, endpoint
ID, operation, request/response TypeIds and wire sizes by value. Resolve these
from live schema metadata; a caller-supplied `RuntimeEndpoint` POD is not proof
of permission. Retaining a metadata pointer or copying its borrowed name does
not extend its lifetime. A binding from another client or admitted session is
rejected even when its endpoint and type IDs coincide.

| Operation | Request type | Response type | Permission |
| --- | --- | --- | --- |
| FieldRead | Void | Field type | Readable Field |
| FieldWrite | Field type | Void | Writable Field |
| Command | Declared request | Void | Command category |
| Service | Declared request | Declared response | Service category |

Before enqueue, check the binding, endpoint category and capabilities, exact
types and wire sizes, and canonical input representation, including Boolean
bytes limited to 0/1. Void and a zero-byte struct remain distinct types.
Reuse Runtime's value validation; do not duplicate the TEQ1 or RPC parser.

## Execution guards and invalidation

All DynamicClient calls, schema reset/reparse, backend operations and callbacks
run in one owner context in the first version. The final binding check and
handoff authorization run in that same controlled path without intervening
owner-event processing. Existing ThreadSafeClient queue locking does not make
runtime metadata access or reset safe for concurrent producers.

Check current schema/session before enqueue, inside FIFO activation immediately
before `backend.start()`, before every `prepareHandoff()` including retries,
and before creating a response view. Guard every core entry point that can
activate work through `process()`, including `nextPacket()` and `onReply()`;
checking only an outer event loop or the completion callback is insufficient.

Invalidation immediately stops admission, activation of queued work and all new
handoffs, including retries. It is irreversible for that admitted binding. An
already granted execution-capable handoff remains evidence of possible execution
even if no send-accepted notification arrives. Preserve the backend's existing
classification: a non-final RPC WRITE alone does not establish possible endpoint
execution, whereas a previously authorized final WRITE does. Invalidation cannot
recall a port-owned TX copy that was already authorized.

| Request phase at invalidation | Required behavior |
| --- | --- |
| Queued | Complete without starting the backend |
| Active, no execution-capable handoff ever authorized | Forbid handoff and finish without claiming possible mutation execution; retain any transaction cleanup requirements |
| Execution-capable handoff authorized | Retain evidence of possible execution; stop new handoffs |
| Full correlated result processed before deadline | Preserve actual operation result |
| Deadline expires without a result | Preserve backend timeout/uncertain-outcome semantics |
| Completion with stale binding | Do not create DynamicValueView or publish cached values |

Do not add unconditional stale cancellation ahead of backend reply processing.
`DeviceClient::onReply()` currently calls `process()` first to enforce deadline
precedence. A reply processed after the deadline must not become successful
because its bytes arrived in an RX buffer earlier. Within that deadline, stale
schema alone must not prevent processing a correlated operation result while
the transaction is still live. Keep send-accepted evidence and an already
checked completion; do not reopen a completed request or call its callback twice.

For the first version, invalidation permits passive completion processing but
no further physical sends. RPC may need additional READ packets to obtain the
complete result. If they were not authorized before invalidation,
bounded timeout and required session recovery apply instead. A partial RPC ACK
is not a complete Command/Service result. `Packet::mayExecute == false` alone is
not a post-invalidation send permission: non-final request WRITE packets also
have that flag. Any future settlement-only send rule needs an explicit contract
and separate tests for the retained transaction.

## Operation result and value availability

The dynamic completion exposes the original operation/transport result and a
separate runtime value status. Preserve backend status, dispatch status,
endpoint status and recovery requirements. In particular, `OutcomeUnknown`
must not be replaced by `Stale`, and Command Accepted must not become Executed.
After possible mutation execution, expiration without a checked result retains
`OutcomeUnknown`. A FieldRead can still time out without an uncertain mutation;
RPC session recovery may nevertheless be required.

A current, successful non-Void response is validated through
`RuntimeSchema::view()`. A stale binding or invalid payload yields no value view
without erasing the operation result. Void responses need no value view.
Every accepted request receives exactly one completion in the owner context;
failed submissions return their rejection directly without a later callback.

Response spans and their value views borrow backend storage only until the
completion callback returns. For deferred work, first copy bytes to caller-owned
storage. Recheck schema/session when consuming deferred data, even after copying
the bytes. The workspace must remain alive; descriptor/index storage stays
immutable for its published generation and is invalidated before release.

## Lifecycle and quiescence

```text
Admitted -> Invalidated -> Draining / Recovery -> Quiescent -> New Admission
```

While requests, producer calls, callbacks or physical TX remain outstanding,
the binding can only be invalidated, never replaced or revived. A single
immutable binding removes the need for a per-record schema stamp. If concurrent
runtime producers are introduced later, any per-request stamp must be captured
before FIFO publication; attaching metadata after `submitEncoded()` returns
cannot establish this guarantee.

Quiescence requires completed producer calls, callbacks and logical operations,
necessary backend recovery and actual driver TX/DMA release. Neither
`pending() == 0` nor `DeviceClient::drained()` alone proves physical transport
quiescence. The application/port must provide that evidence before new admission
or storage reuse. For uncertain RPC execution, close/drain the old remote session
and call `confirmSessionClosed()` only after the existing backend contract allows
it. Merely resetting a client or admitting a Descriptor does not clear recovery.

No old request is migrated or replayed in a new session. New execution requires
a new explicit user submission. Readmission is not `DeviceClient::close()`
reversal: close remains terminal for the core instance, so a closed core must be
drained and replaced before reuse.

## Acceptance evidence for the implementation PR

The following tests are requirements, not results already obtained:

| Scenario | Required observation |
| --- | --- |
| Same IDs, different schema/types | Old queued request never starts or sends |
| Reset between nextPacket and prepareHandoff | Handoff refused; preserve evidence from any earlier handoffs |
| Reset before any execution-capable handoff | No false OutcomeUnknown merely from a non-final RPC WRITE |
| Reset after execution-capable authorization but before send notification | Possible mutation execution remains recorded |
| Valid reply before deadline after reset | Actual operation result retained, value marked stale |
| Reply processed at/after deadline | Deadline result wins; no false Ok |
| Lost reply and retry after invalidation | No new physical send; uncertainty/recovery retained |
| Multi-packet RPC invalidation | Every handoff guarded; no partial ACK mistaken for final result |
| Old reply after new admission | Backend correlation rejects it; no old value published |
| Foreign binding/handle with identical IDs | Rejected before enqueue |
| Invalid Boolean, capability, type or size | Rejected before enqueue |
| Command Accepted | Accepted retained separately from Executed |
| Callback returns and response storage is reused | No borrowed value used by deferred UI work |
| Empty FIFO while TX/DMA remains active | New admission and buffer reuse refused |
| Invalidation/close with accepted work | Exactly one completion per accepted request |
| Static Generated SDK on default policy | Existing exact admission and Codec behavior preserved |

Exercise the real Host Model through the same DeviceClient FIFO with Encoded,
Retrying Encoded and RPC. Include fixed-storage/no-heap checks, negative module
dependency checks and the existing regression matrix. Distinguish host transport
tests and offline ARM compilation from physical STM32/DMA qualification.
