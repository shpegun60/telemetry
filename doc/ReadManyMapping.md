# ReadMany mapping: bounded prototype and proposed direction

Status: **experimental comparison; no production wire assignment or runtime
ReadMany API is approved here**. The accepted [logical contract](ReadMany.md)
remains the basis. The [runnable prototype](../examples/read_many_mapping/README.md)
compares an optional compact read operation with an ordinary application Service.
All production library modules and existing wire parsers remain unchanged.

## Proposed direction

Prefer an optional dedicated read-only operation for a changing Workbench watchlist
if its measured benefit justifies integration cost. Keep existing FieldRead for
single Fields and explicit application fallback, aggregate Fields for coherent or
fixed groups, and ValuesFile when most Fields are needed. Do not silently fan out
an unsupported batch into another queue. This prototype is the evidence stage;
it does not commit a production operation number or protocol version.

The application Service runs through existing TEQ1 Service processing. Its fixed
DTO follows the existing exact-size RPC contract when mailbox capacities permit;
this prototype does not execute an RPC round trip. It retains potentially-mutating
timeout semantics. The compact experiment is complete-frame only: unchanged
RpcClient accepts four operations and requires exact successful response size.
Variable per-item response lengths need an explicit future RPC decision.

## Capability experiment

An application-owned BytesFile exposes a 24-byte `RCP0` record through ordinary
Resource LIST/READ; Resource needs no knowledge of Telemetry. The sample uses
`/prototype/read-many.cap`. Its record contains version 0, maximum item count,
read-only scalar/enum profile flags, request/reply payload ceilings, session and
Descriptor fingerprint. Unsupported flags/limits or mismatched admission are
refused explicitly. A local capture copies IDs/types and retains schema generation;
serialization and response validation require the current capability.

The sample's known-ID READ costs 61 actual framed bytes. This excludes LIST,
path resolution, Descriptor download and session establishment. The file ID/path
and publication policy are application-defined, not newly reserved Resource IDs.
An implementation must retire this capability when its Model/session changes and
finish transport quiescence before a new admission.

## Experimental byte layout

`RMX0`, version 0 deliberately differs from TEQ1. Existing parsers reject it; it
does not work through the current TCP router, Nucleo allowlist or RpcClient.

| Offset | Experimental header field |
| ---: | --- |
| 0 | `RMX0` magic, 4 bytes |
| 4 | Version 0, one byte |
| 5 | Direction: request 0 / reply 1 |
| 6 | Global status; requests require zero |
| 7 | Reserved zero |
| 8 | Nonzero session, U32 LE |
| 12 | Nonzero request identity, U32 LE |
| 16 | Descriptor fingerprint, U64 LE |
| 24 | Exact payload length, U16 LE |
| 26 | Reply payload budget ceiling, U16 LE |
| 28 | Four reserved zero bytes |

Request payload: U16 count followed by 1–32 U32 packed Field IDs. Reply payload:
U16 count, then ordered records of U32 ID, U8 item status, reserved zero byte,
U16 value length and canonical value bytes. Scalar/open-enum payloads are at most
eight bytes. Unknown, unsupported, unavailable and failed reads have zero value
length and no TypeId. Successful bytes are checked using captured schema types.
Global preflight failures may produce a correlated header-only error.

The profile bounds are 130 request / 514 reply payload bytes, or 162/546 including
the experimental headers. The response budget is a ceiling **in this experiment**;
legacy FieldRead/Service exact-size validation is unchanged. Count, order, IDs,
statuses, reserved bytes, sizes, canonical payload and complete consumption all
pass before any borrowed reply view is returned.

Metadata-only preflight reserves the entire worst-case response and maximum
remaining scratch before the first Field getter. Duplicate IDs fail globally.
Unavailable/error results publish no partial thunk bytes. Reads remain sequential,
without atomic snapshot guarantees. The admitted fingerprint must match the supplied
Model; admission owns that relationship instead of rescanning the Descriptor.

## Traffic and implementation cost

The executable counts actual COBS output, including length, CRC and delimiter:

| Selected U32 values | Compact experiment | Fixed 32-slot Service | Individual reads, 80*N |
| ---: | ---: | ---: | ---: |
| 1 | 96 B | 720 B | 80 B |
| 8 | 208 B | 720 B | 640 B |
| 12 | 272 B | 720 B | 960 B |
| 16 | 336 B | 720 B | 1,280 B |
| 32 | 592 B | 720 B | 2,560 B |

The [PR #13 baseline](ReadComparison.md) measured eight individual reads at 640 B;
other individual counts here are extrapolated. The new compact/Service columns
come from actual prototype framing. Mandatory zero bytes prevent long COBS runs,
so the observed 32-item costs are below the earlier generic 593/722-byte bounds.

Sparse ValuesFile already costs 221 B cold or 197 B warm for eight selected of 32,
plus a separate 61-byte warm setup; it calls all 32 getters. Compact ReadMany's
potential value is avoiding unused getters and preserving per-item outcomes,
not universally minimizing bytes. At 115200 baud, the observed 592-byte frame
pair consumes about 51.39 ms of 8N1 airtime before getters or scheduling. Ten Hz
remains conditional; one compact item is larger than one ordinary FieldRead.

The Service must encode every fixed slot. It also stages active IDs outside its
decoded Workspace and packs value bytes into its response DTO. The report counts
these explicit copies separately from retained GUI-like scalar copies; it does
not claim complete memory-traffic accounting. Zero/128-iteration synthetic getter
work tests sensitivity, not real device CPU or physical latency.

Object sizes, explicit buffers, selected-root ARM sections and individual stack
frames are retained by the [qualification runner](../tests/read_many_mapping/README.md).
They are not full firmware RAM/Flash or call-chain peak stack measurements.

The clean code revision `17d1bc3` passed 595,791 checks per host profile on
GCC 13.3, Clang 18 ASan/UBSan with leak checking, and Windows MinGW 13.1. Each also
passed the 55-check no-heap probe and six expected allocation-trap controls. The
unchanged baseline example passed its 194,329 checks through the shared runner.

Offline ARM GCC 13.2.1 produced these separate GC-selected images:

| Optimization / mapping | Linked `.text` | `.rodata` | `.data` | `.bss` | Entry frame only |
| --- | ---: | ---: | ---: | ---: | ---: |
| `-Os` compact | 5,568 B | 2,156 B | 0 B | 816 B | 232 B |
| `-Os` Service | 7,844 B | 3,380 B | 96 B | 1,472 B | 272 B |
| `-O2` compact | 5,604 B | 2,156 B | 0 B | 816 B | 264 B |
| `-O2` Service | 7,768 B | 3,400 B | 96 B | 1,472 B | 304 B |

These totals include fixture/application code, not an incremental cost estimate
for an existing firmware. The individual entry frames exclude their callees.
Both images use installed ARM packages and contain no allocation symbols.

On the measured x86-64 ABI, Capture is 432 B and Batch is 516 B. The 9,928-byte
Context already contains 4,456-byte schema metadata, 2,048-byte request and reply
buffers, 690-byte Workspace storage and Batch. Other caller locals, including
reply views, are additional. The native fixed request/reply objects are 132/516 B,
while their canonical wire payloads are 130/514 B. These are accounting components,
not a full stack or application RAM bound.

## Required production work

Before integration, approve capability discovery/lifecycle and a production mapping
for each supported transport. Preserve existing tags, reserved bytes and Native API.
Any dedicated read operation needs explicit read-only backend classification;
physical timeout recovery remains independent. Parser/routing allowlists and
response-length semantics must agree on both ends. RPC support is not inferred
from having a similar header size.

Then add one captured batch request/result policy to the existing FIFO, preserve
activation/handoff/session guards, and integrate one pending Workbench watch cycle.
The prototype has no execution queue, cancellation, deadline, retries or transport
driver, and does not qualify those future state-machine paths. No production
DynamicClient, Workbench, Command or Service behavior changes in this stage.
