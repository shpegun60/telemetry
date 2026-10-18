# RpcClient / RpcSessionPool qualification

[Client API](RpcClientV1.md) · [Session API](RpcSessionsV1.md) ·
[Client example](../examples/rpc_client/README.md) · [Four clients](../examples/rpc_multi_session/README.md)

## Revision and scope

Implementation started from clean local/remote
`f615775bf3dd4c7c4e12ac047e60b111e52ce0dd`. During this slice, the independent
evidence-only commit `aff0f15edc9148850dd2235ee7e59908a1ced14d` was published;
it is retained as the parent baseline. It changed only the preceding delegate
firmware-equivalence record and its README. The pinned delegate v1.2.1 remains
intact. Captures honestly record their pre-publication HEAD/dirty paths and
freeze actual inputs independently of HEAD. The final publication SHA is the
commit containing this report, not a claimed hardware measurement revision.

This adds one outgoing typed client and a bounded manager of existing incoming
mailboxes. RpcMailbox behavior, RPC wire v1.0, Resource v1, Codec bytes,
Model/endpoint ABI, TypeRegistry, Descriptor/Values layout and vendor bytes
are unchanged. No new device execution is claimed.

## Files and public API

| Files | Responsibility |
| --- | --- |
| `lib/client/rpc/RpcClient.hpp`, `.cpp` | Typed bindings, one asynchronous transaction, packet scheduling, diagnostics and caller-owned decode |
| `lib/integration/telemetry/v3/RpcSessionPool.hpp` | Fixed bindings, admission handles, generation/ticket checks, ACK gate, bounded dispatch and release |
| `lib/telemetry/codec/codec.pri`, `lib/resource/resource.pri` | Independent header-only Codec and client/server/pool selections |
| `tests/structured/rpc_client/`, `rpc_sessions/`, `rpc_extension.py` | Runtime, no-heap, negative, exhaustion, ODR and offline ARM gates |
| `examples/rpc_client/`, `rpc_multi_session/` | Executable typed calls and four-session integration |
| `tests/structured/resources/` qmake fixtures | Eight source/object selections, repeated inclusion and runtime smoke |
| `.github/workflows/ci.yml` | Host/ARM extension gates and retained artifacts |
| `tests/resources/evidence/{equivalence.json,README.md}` | Fresh old-image equivalence and reproduction |
| Root, architecture, user/resource and suite READMEs | Navigation to API contracts and runnable examples |

The [client API table](RpcClientV1.md#public-api) covers begin, packet handoff,
reply pairing, deadlines, lifecycle and both decode forms. The
[pool table](RpcSessionsV1.md#public-api) covers acquire/valid/handle,
replyAccepted/process/release and their distinct statuses.

```cpp
namespace rpc = resource::telemetry::v3;
struct Add { std::uint32_t amount; };
struct Report { std::uint32_t total; };
constexpr ::telemetry::client::rpc::ServiceEndpoint<Add, Report> add{0, ::telemetry::client::rpc::trustedRpcMapping};
// After application schema agreement and connection admission:
client.process(nowMs);
const auto admitted = client.startSession(1);
if (admitted == ::telemetry::client::rpc::RpcClientStatus::Ok) {
    const auto started = client.beginService(add, Add{7});
    // Check started; feed sends/replies and inspect diagnostic()/decodeResult().
}
```

Complete examples check every returned status. Server setup keeps stable
mailboxes and separate request/result storage, sharing Model/Workspace:

```cpp
rpc::RpcSessionPool<2> sessions{std::array{
    rpc::RpcSessionBinding{mailboxA, filesA.view(), 0},
    rpc::RpcSessionBinding{mailboxB, filesB.view(), 0}}};
const auto session = sessions.acquire({peer, connectionGeneration, channel});
// handle() -> complete ACK handoff -> replyAccepted() -> process().
```

## Executed gates

Counts below are per executed mode, not unique scenarios or compiler
invocations. Coverage floors are stored separately; reduced floors and invalid
reports are rejected by controls. Final captures require unchanged inputs.

| Gate | Measured result |
| --- | --- |
| New client host mode | 1730 + 1730 + 1730 fixture conditions, 16 exhaustion controls, 45 example conditions = **5251** |
| New pool host mode | 498 + 498 + 498 fixture conditions, 28 exhaustion controls, 247 example conditions = **1769** |
| New compile-negative cases | 11 client and 8 pool cases, intended diagnostics required |
| Host configurations | Qt GCC 13.1 release/null checks; Linux Clang 18 release/ASan+UBSan |
| Final new-component matrix | **12 configurations**, 418 commands, 114 intended compile refusals, 48 host executions, **28,080** conditions; all current input hashes agree |
| Existing RpcMailbox, four host modes | **31,060** conditions, 16 executions; input guards unchanged |
| Existing Resource core | GCC and sanitized Clang: **316,118** conditions each; offline ARM passes |
| Existing Codec / Model / Descriptor / Values | GCC, sanitized Clang and offline ARM pass, including existing rejection/oracle/ABI gates |
| User-guide examples | 10 executables, **396** conditions |
| qmake selections | **8/8** built and executed; source/object selection and input stability verified |
| Existing endian gate | 12 native LE/s390x BE executions; five complete descriptor/value files agree |
| New real BE loopbacks | QEMU s390x: client **1730**, pool **498**, zero failures |
| Old firmware equivalence | **12/12 raw binaries** byte-identical to retained artifacts; 145 library + 57 fixture inputs frozen |

Host fixtures use local-object budgets 0 and 32, then repeat the default with
allocation traps. Isolated exhaustion probes exercise real u32 boundaries in
one translation unit without adding production inspection hooks. Multi-TU
checks instantiate public APIs independently and verify client type-token
agreement at a real link.

## Offline Cortex-M7

CubeIDE GCC **14.3.1** compiles and links O2/Os/Og normally and with retained
null checks. ARM runs execute zero conditions. Real operation roots and
compiled implementation boundaries must remain linked. Allocation/formatting
inspection, actual newlib controls and a compiled 4 KiB frame refusal stay
enabled. Existing RpcMailbox ABI mismatch gates pass. The compiled client has
no configuration-dependent layout boundary; no artificial mismatch gate is
claimed for it.

Representative production-root fixtures, **not incremental application costs**:

| Component | O2 `.text` | Os `.text` | Og `.text` | Largest root frames O2/Os/Og |
| --- | ---: | ---: | ---: | --- |
| Client | 4464 B | 3668 B | 6008 B | 80 / 88 / 80 B |
| Pool + server composition | 7056 B | 5860 B | 8992 B | 112 / 96 / 128 B |

Largest client implementation frames are 80/96/144 B. Existing mailbox process
frames are 224/200/200 B, protocol 104/104/128 B, Model adapter 48/40/112 B.
Gates retain the ordinary 192 B ceiling and named existing mailbox/adapter
exceptions of 256/320 B. These are individual frames, not nested call-chain
bounds.

Independent ARM object/objdump measurement: `sizeof(RpcClient)=128`,
`sizeof(RpcMailbox)=56`, pool `<1>=52`, `<2>=96`, `<4>=184`, binding 16,
handle 24, reply ticket 16 bytes. These exclude caller storage and refer to
this ABI/toolchain. Fixture BSS includes explicit buffers and is not the cost
of one instance. No MCU cycles or physical transport measurements are claimed.

## Findings closed

- Accept a first Ready READ containing only its 32-byte header; reject a later
  zero-progress continuation.
- Reject a Lease decode pointer variable overlapping client storage before
  assigning through it; preserve all retained bytes on refusal.
- Keep retry bytes/tickets/cursors/final boundaries exact. Reset packet tickets
  only in a strictly newer session generation; never wrap counters.
- Reject scalar Command/Service signatures unsupported by native factories.
- Keep the example clock monotonic across calls; demonstrate a lost ACK,
  one callback and schema mismatch before admission.
- Scope the Clang diagnostic for test-only private-state counter probes.
  Constant-initialize ARM fixture file tables rather than weakening the
  formatting-symbol gate.
- Clarify first READ, ACK tickets, constructor reset, exact statuses, buffer
  lifetime and fresh ticks after synchronous callbacks in documentation.

## Artifacts and limits

Local artifacts are retained outside the repository at
`C:/Users/admin/Documents/telemetry-validation/20261018-rpc-extensions`:
final client/session summaries, commands, binaries, disassembly, `.su` frames,
qmake manifests, old regressions, big-endian execution, sizes and equivalence.
`matrix-summary.json` selects the six final client reports under `final2/` and
six pool reports under `final/`, verifies their current input hashes, and retains
their summary digests. Earlier diagnostic runs remain archived and are excluded
from completion counts. It records 12 retained ARM production images, plus the
separate ODR/example links. Individual final runs took 37–76 seconds on the
recorded local host; these wall times are not MCU operation latency.
CI retains corresponding exact-revision artifacts. Local CubeIDE evidence
and CI's installed GNU Arm compiler have distinct recorded identities.

The equivalence record compares actual firmware bytes, not just published hash
strings. Six of twelve ELF files also match; the other six retain separate
identities without a claim of equality. All 23 historical receipt JSON files
remained byte-identical. The capture records source HEAD/dirty state and all
202 normalized-LF library/fixture input hashes.

Application owns schema agreement, framing, ingress identity, external reply
correlation, bounded queues, time and synchronization. One owner serializes a
shared Workspace. Client backing buffers outlive the client; mailbox backing
resources outlive the pool. Resource replies have no request ID. Disconnect
after possible final acceptance can leave OutcomeUnknown; reconnect never
triggers hidden replay or guarantees exactly-once execution. Pool capacity,
generation and tickets are bounded without wrap. No board was accessed.
