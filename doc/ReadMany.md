# Bounded ReadMany: proposed decision

This document records the initial design decision. The optional
[production C++ Encoded mapping](ReadManyEncoded.md) now implements the scalar
profile; GUI watchlists, RPC and JS/TS batch calls remain future work. The
[read comparison](ReadComparison.md) measures existing paths. This proposal
addresses a changing Workbench watchlist without publishing a new aggregate
Field for every selection. It does not add WriteMany or Command/Service batching.

See the [bounded mapping prototype](ReadManyMapping.md) for the next experimental
comparison. Its experimental bytes remain separate from the production mapping.

## Workload and limits

These are project assumptions, not measurements of user demand or hardware:

| Property | Initial target |
| --- | --- |
| Selection | 1-32 Field IDs across catalogs; typically 8-16, example 12 |
| Polling | Normally 1-5 Hz; 10 Hz where the complete transport/processing budget permits |
| Types | Existing canonical scalar types, including enums; structures/arrays require a separate byte-budget decision |
| Memory/execution | Bounded caller-owned storage, no required heap, one existing FIFO and owner loop |
| Consistency | Sequential reads; no atomic/coherent snapshot guarantee |

`MaxItems = 32` is a hard first-version ceiling. `MaxRequestBytes`,
`MaxResponseBytes`, supported types and scratch limits must also be explicit and
admitted for the session. Count alone cannot bound an arbitrary DTO. The current
serial profile has a 2048-byte body and 2016 bytes after a TEQ1 header; those are
profile limits, not universal library limits. Polling must fit both directions,
getter work, scheduling and other traffic. Ten Hz is not promised at every baud.

## Minimal logical contract

- One accepted batch uses the existing DeviceClient FIFO and one completion.
  Reuse FieldIndex/Model getters and canonical Codec bytes. Do not introduce
  another endpoint registry, dispatcher system or execution queue.
- Preserve request order. Reject zero/more-than-32 IDs and duplicates globally
  before any getter; at most 496 pair comparisons suffice without a heap/hash map.
- Preserve one ordered item result per requested ID. Server-side unknown IDs
  produce `NotFound`; unsupported types and unavailable reads produce item errors
  without success payload. Current Fields are declared readable; temporary
  unavailability is not a Descriptor permission bit or a global batch failure.
- The schema-aware client resolves IDs/types against its admitted schema before
  enqueue; unresolved local IDs are refused synchronously. Server `NotFound`
  remains a defensive result for raw clients or a failed remote lookup.
- Keep transport/operation outcome, endpoint item status and value validity
  separate. A stale schema prevents views, without erasing the actual operation
  outcome. Validate the whole reply's bounds/count/order/correlation before
  exposing any value. Never publish partial or uninitialized bytes from a failed item.
  Error items have no value and need no schema TypeId; successful payloads are
  validated against the captured request types, not trusted reply-supplied types.
- Capture an immutable request list tied to schema generation and session.
  Reuse the existing activation/handoff/lifetime guards. No fictitious Struct
  TypeId represents heterogeneous results, and static Codec checks stay intact.
  Views are borrowed for the callback; a GUI copies retained payloads beforehand.
- A new logical operation must be explicitly classified as read-only in every
  backend. Current Encoded/RPC timeout paths treat non-FieldRead operations as
  potentially mutating. Read timeout must not acquire `OutcomeUnknown` solely
  because a new tag was added; existing physical recovery/quiescence still applies.
- If the admitted peer/profile lacks the capability, submission is explicitly
  refused before enqueue. The caller may choose individual reads, aggregate or ValuesFile;
  there is no hidden fan-out queue or automatic fallback after handoff.

## Preflight before the first getter

Preserve existing envelope/session/minimum-output/overlap checks and their
precedence. Within a future batch body: validate structure/count, duplicates,
lookup/type eligibility, then the complete worst-case reply and scratch budget.
Use checked arithmetic and server metadata, not client-supplied sizes or getter
calls. Reserve all item metadata and every eligible success payload even if a
getter may later be unavailable. Sequential scratch is the maximum requirement,
with alignment and current Workspace usage accounted for, not a sum of DTOs.

Malformed input, duplicate IDs, overflow, overlap or insufficient total capacity
invoke no getter. A structured error reply is allowed where the envelope permits.
After reading begins, item failure does not roll back previous reads. Coherent
acquisition remains an application-defined Aggregate/Snapshot responsibility.

## Cost model, not a wire specification

For comparison only, assume one 32-byte TEQ1-like envelope in each direction,
two count bytes per body, four request bytes per ID, eight metadata bytes per
result, and four scalar payload bytes. No operation number, record encoding or
new protocol version is selected by these assumptions.

Raw TX+RX is `68 + 16*N`. Apply the existing serial profile's worst-case COBS
bound to each body, including its length, CRC and delimiter:

| N, 4-byte values | Individual framed bytes (extrapolated except N=8) | Candidate framed upper bound | Candidate airtime at 115200, 8N1 |
| ---: | ---: | ---: | ---: |
| 8 | 640 | 208 | 18.06 ms |
| 12 | 960 | 272 | 23.61 ms |
| 16 | 1280 | 336 | 29.17 ms |
| 32 | 2560 | 593 | 51.48 ms |

Only the eight-value existing-read baseline is measured here; other individual
counts are linear extrapolations. All candidate bytes/airtime are arithmetic.
The eight-value estimate saves 67.5% against the measured 640-byte cycle, while
the existing fixed aggregate is still smaller at 108 bytes. The 32-item bound
includes an extra worst-case COBS code byte. RPC/chunked mapping, CPU, scheduling
and physical-driver costs are not included.

For eight selected scalars out of 32, the measured ValuesFile uses 221 framed
bytes cold or 197 warm, plus a separate 61-byte warm setup. Thus the 208-byte
candidate is not a universal traffic win: it is only 13 bytes below cold ValuesFile
and 11 above warm. Its potential benefit is avoiding the other 24 getters and
retaining per-item outcomes for an arbitrary selection. Measure realistic getter
costs and selection changes before choosing it over ValuesFile.

With the same illustrative metadata, 32 eight-byte scalars need 130 request and
514 response payload bytes: 644 bytes before envelopes/copies, at most 722 framed
bytes. IDs alone consume 128 bytes. Captured binding metadata, callback storage,
queue capacity, TX leases, RX and scratch are additional; do not assume they fit
the current callback inline budget or multiply 32 full-size DTO buffers.

## Mapping and implementation cost

Evaluate two mappings before implementing either:

- An application Service reuses current envelopes but fixed-size Descriptor DTOs
  can transmit padded ID/result arrays in full. A 32-slot Service with the above
  eight-byte-value budgets can approach the 722-byte bound even for eight requested
  items. Current Services also use potentially-mutating timeout semantics and are
  blocked by the Nucleo read-only allowlist. This is not a free compact ReadMany.
- A dedicated optional read mapping can avoid unused DTO padding, but requires
  capability/admission, framing/parser allowlists, backend read-only classification
  and heterogeneous result validation. Reuse the current FIFO and FieldIndex.

Do not put IDs in the current FieldRead payload, reserved bytes or Descriptor
Field capability bits. Existing TEQ1/RPC parsers reject such requests. Before
submission, select an explicitly agreed session/profile capability; choose its
advertisement and transport mapping in the implementation proposal. No automatic
TEQ1 v2, new Resource protocol or generic Service purity rule follows from this ADR.

The implementation budget includes bounded request/result storage, immutable
schema/session binding, complete preflight, backend classification, explicit
capability refusal and one pending Workbench poll cycle. Missed poll ticks must
not accumulate a hidden backlog. Measure total RAM, ARM text/stack and copies
before acceptance; no byte/LOC/time estimate substitutes for that prototype.

Acceptance must cover count 0/1/32/33, cross-catalog IDs, duplicates, unknown IDs,
unavailable items, type/byte limits, overflow/overlap and zero-getter global failures;
also queue refusal, deadline boundaries, session invalidation, stale replies,
exactly-one completion, no-heap operation and byte-exact client/peer equivalence.
Extend the comparison with the selected mapping and realistic getter costs.

References: [FieldIndex](../lib/telemetry/field/FieldCatalogs.hpp),
[Field metadata](../lib/telemetry/field/FieldTable.hpp),
[Workspace](../lib/telemetry/codec/Workspace.hpp),
[runtime bindings](../lib/integration/dynamic/DynamicClient.hpp),
[TEQ1](../lib/wire/EncodedWire.hpp), [RPC](../lib/client/rpc/RpcClient.cpp),
[Encoded backend](../lib/integration/encoded/Backend.hpp),
[RPC backend](../lib/integration/rpc/Backend.hpp),
[serial profile](../examples/inspector_serial/SerialProfile.hpp),
[DynamicClient lifecycle](DynamicClient.md).
