# Simpler host integration: scope and acceptance

The goal is a short application path from a Model or Descriptor to device I/O
and an embedded UI. Advanced contracts remain available for custom transports,
explicit buffers and recovery. This is an implementation sequence, not a claim
that every stage below is already present.

## Reviewed baseline

Three independent reviews covered architecture, a programmer consuming the
packages, and lifecycle/resource boundaries. The proposal to coordinate an owned
connection and reuse providers is sound. Several assumptions needed correction:

| Proposal assumption | Verified distinction |
| --- | --- |
| Each Web panel needs a new independent engine | Existing independent providers can share one client; 86 consumer assertions checked separate selection/drafts and one active exchange |
| Static always means Generated SDK | Static C++ can use handwritten known types and Codec; generated types are one optional route |
| A generated Static UI needs only codecs | Structural metadata is optional for typed I/O, required for generic UI; preservation-safe editing also needs wire-editing codecs |
| Static and Dynamic results are interchangeable | Their operation facts agree, but result shapes differ; the Session normalizes interpretation separately from operation facts |
| Closing a socket proves recovery | It proves local closure; remote quiescence/supersession remains a transport/server-specific contract |
| Add a provider around every C++ Workbench | Workbench currently owns Inspector/DynamicClient; sharing needs a borrowing seam, not another owner/FIFO per panel |
| Simple API will cover 80–90% of projects | That is a design target, not a measured coverage result |

The 86-assertion provider experiment used generated modules and a real SDK client
with a controlled backend, not a browser/network or physical-device test.

## Stage 1: SDK connection Session

Implemented in [ConnectionSession](ConnectionSession.md): optional host JS/TS
`connectStatic` and `connectDynamic`, one private existing core/FIFO, normalized
results, request cancellation and explicit lifecycle. `demoWebSocket` selects the
existing reference server's bootstrap/lease contract. Custom hosts can implement
`ConnectionProfile`. No transport, authentication or discovery scheme is inferred
from a URL alone.

Acceptance includes a packed offline-installed downstream consumer, exact emitted
TypeScript artifacts, controlled timeout/cancellation/observer transitions,
pre/post-handoff outcomes, actual local closure and same-peer supersession,
native Encoded operations, and a real Chromium/reference-server journey.
Session ownership is not a second execution engine. Core wire, Codec and FIFO
implementations remain unchanged.

## Stage 2: UX session and independent views

Still planned: connect an installed UX adapter/provider to the owned Session,
then provide independent endpoint selection, drafts, response snapshots and
diagnostics over the same owner/FIFO. Views borrow execution and own presentation
state; destroying a view does not shut down the shared Session. A mutation must
have an explicit sibling-freshness policy. Write plus readback stays two ordered
operations, not an atomic compare-and-write transaction.

The installed Dynamic QML provider needs a compatible borrowing boundary in the
current Workbench/Inspector ownership. It must not instantiate a second client
inside each view. Static QML providers already have per-instance presentation
state; actual downstream tests must establish what can be reused before adding
new wrappers. User-facing QML and Web remain in Telemetry UX.

Acceptance: two panels with different selection/drafts, one active exchange;
view destruction during an accepted operation; session replacement and stale
callbacks; exact U64/S64/open enum; no automatic mutation replay; installed
downstream application without knowledge of source-internal paths.

## Stage 3: starters, semantic metadata and diagnostics

Still planned: complete application starters that select a documented connection
profile and mount existing shared views. Keep labels, units, ranges, initial
editor values and help as optional presentation metadata, separate from the
structural Descriptor and wire compatibility. Unknown metadata must not weaken
Codec validation. Initial editor values are not device defaults.

Acceptance: a programmer starts from the documented installed packages, declares
a small device, generates the optional SDK/UI entry point and performs an actual
read/edit/write/readback without implementing another queue or copying library
internals. Runtime/device work is reported separately from host demonstration.

STM32-hosted HTTP/WebSocket, additional transport profiles and concurrency across
unrelated clients remain separate tasks with their own server/access and recovery
contracts. Existing asset export is not itself a firmware web server.
