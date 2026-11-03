# Telemetry SDK and Telemetry UX roadmap

This is an implementation roadmap, not a claim that the planned packages or APIs
already exist. The reviewed starting revision is `62b5bd79d0501a671cf2e0e0d2492b6d91d28bbd`.
The current repository/package/namespace names remain valid during this work.

## Product boundary

**Telemetry SDK** owns Model, Native/Encoded API, Codec, Descriptor/RuntimeSchema,
Resource, Encoded/RPC, DeviceClient, DynamicClient, ReadMany/Batch, transport
contracts, and the C++/JS/TS SDK generator. Clients must remain usable for
STM32-to-STM32 communication without a display, browser, Qt, heap or RTOS.

**Telemetry UX** will own ConfiguratorSession presentation state, UI schema
adapters/hints, QML adapters and components, HTML Web Components, editors, themes,
custom panels, UI starter projects and generation of self-hosted web assets.
Its only dependency direction is **UX -> SDK**. A JS codec/client is SDK code;
DOM, QML objects and forms are UX code, regardless of their present directories.

The generator is an optional host-tool distribution, not a firmware runtime
dependency. DTO, codec and client-binding generation stays in SDK. UI/QML
generation will become an optional UX extension using the same validated
descriptor IR. A transition distribution must preserve the existing QML CLI
and output; removing that distribution requires an announced compatibility
change. Minimal C++/JS/TS generation must work without importing UX.

The repositories are now `telemetry-sdk` and `telemetry-ux`. The SDK rename
preserves its history, issue and PR identities. Current remotes and documentation
use the canonical name; sealed historical evidence keeps its original identity.
Do not rename namespaces, include paths or CMake targets merely to rename the
product. Existing portable Inspector/Workbench public targets initially stay
available and are reused by UX; their ownership logic must not be copied.

## Delivery order

| Stage | Deliverable | Acceptance |
| --- | --- | --- |
| S1 | Optional typed C++ ReadMany over the existing FIFO | Generated and handwritten Codec, actual Model bytes, per-item outcomes, noheap, isolated installed consumer and M7 checks; no RuntimeSchema dependency |
| S2 | Typed C++ Mixed Batch builder | Typed requests/responses, bounded caller storage, ordered repeated IDs, full preflight, independent outcomes and preserved uncertainty |
| S3 | JS/TS ReadMany and Mixed Batch | Existing FIFO, TRM1/TBM1, bounded variable replies, typed results, native C++ interoperability and explicit Unsupported RPC |
| S4 | Headless Dynamic JS/TS session integration | Full validated Descriptor, cached bindings, guards at enqueue/activation/every handoff/decode, operation result separate from stale values |
| S5 | Optional structural metadata and client conformance | Generated/runtime metadata equality and the same six operations across static/dynamic C++/JS/TS; installed/noheap/ARM gates |
| U1 | Independent Telemetry UX package and repository migration | Installed versioned SDK dependency, one semantic UI session contract, no SDK-to-UX import, preserved existing consumers |
| U2 | Reusable QML toolkit and providers | Same QML with static/dynamic sessions, independent panels, real input tests, custom delegates/tabs and installed starter |
| U3 | Reusable Web Components and providers | ES modules, same HTML with either mode, custom panels/editors, exact integers, lazy arrays, real browser tests and measured assets |
| W1 | Host self-hosted WebSocket integration | Browser -> SDK -> bounded persistent server -> real dispatcher/Model for all supported operations, session/access/close tests |
| W2 | Concrete STM32 self-hosted port and final packaging | Device serves its own UI and Model, measured Flash/RAM/stack/latency, explicit network hardware qualification, installed SDK/UX examples |

Each stage may contain several small PRs. Do not combine repository migration,
wire changes, several clients and a new GUI into one unreviewable change.
S1-S5 are integrated through SDK PR #33 (merge `b4e372a`), with qualification
records including the [four-surface conformance suite](../tests/sdk_conformance/README.md).
Feature qualification and the CI of a final `main` revision remain separate
evidence: verify the exact merge revision before making a release claim.

U1 now has a separate repository, installed versioned generator extension and
compatibility distribution. UX owns the Inspector app, reusable Qt-only
Configurator view, generated-QML demo/tests, and
[structured browser payload tools](https://github.com/shpegun60/telemetry-ux/blob/a336b8c2984d5adf235b0791b545a37d3da3cc8b/examples/structured_client/README.md). SDK retains its headless
clients/codecs, native fixtures and Qt Core console smoke. Legacy Widgets
source retirement is separately blocked and is not declared complete here.

U2 has the installed reusable QML view and existing dynamic Inspector provider;
the typed/generated static QML provider is still pending. U3 has the shared
ES-module renderer, static/dynamic web providers and optional generated web
starter, but a registered browser Custom Element API is not complete in this
snapshot. Host demonstration HTTP adapters do not complete W1 persistent
WebSocket integration, W2 STM32 network self-hosting or their hardware/resource
qualification. No UART/DMA receipt establishes those network/UI milestones.

## Contracts that every stage preserves

- Static/dynamic selection is explicit per session, without automatic fallback.
  Static checks the admitted schema before execution; dynamic validates the full
  Descriptor first. Existing handwritten typed C++ policies remain supported.
- One underlying client, FIFO and lifecycle owner serves the whole session,
  including custom panels and polling. No component opens a hidden second client.
- Invalidation prevents new execution without erasing known operation outcomes.
  New admission requires logical drain, recovery and actual transport release.
- ReadMany selects 1-32 top-level scalar/open-enum Fields, not struct members or
  arbitrary DTOs. It does not guarantee a simultaneous snapshot.
- Mixed Batch is ordered, without rollback or exactly-once execution. Accepted
  is distinct from application completion. UX never automatically replays an
  uncertain mutation; explicitly selected retrying backends keep their contract.
- TRM1/TBM1 are admitted for the peer/schema/session. Unsupported backends do not
  silently emulate a batch with several requests.
- UI owns its snapshots and transactional drafts. JavaScript Number does not
  preserve every NaN bit: an unchanged value must retain its original wire bytes
  when another leaf is edited. Big integers never pass through QML double.
- Codegen metadata for generic inspection is opt-in. Units, ranges, defaults and
  presentation hints are separate overlays, not invented wire constraints.

## Usability and review gate

Every implementation stage has three independent viewpoints: architecture
review, an end-user consumer and product acceptance. A satisfactory internal
design alone does not complete a stage.

The end-user gate requires a short runnable installed-package example, checked
results, useful diagnostics and no manual payload construction for ordinary
typed calls. New wrappers must remove actual repetition or enforce a necessary
contract. Custom application code stays outside generated/library-owned files.

The target UI journey is connect, select an endpoint, use the generated form,
and see the result. Dirty drafts, invalid input, write outcome, readback failure
and OutcomeUnknown must be clear. The same components must be extensible with
custom tabs/editors rather than requiring a fork. Polling has one pending cycle
and never accumulates missed ticks in a hidden queue.

Host tests use real Models, canonical wire fixtures and controlled clocks.
QML/browser tests exercise actual input and lifetime transitions. An Ethernet
or HTTP hardware claim needs a matching physical port; the existing NUCLEO
UART/DMA receipt does not establish it. Hardware programming is a separate
controlled backup/test/restore operation.
