# Structured resource providers v3.0 (C++20)

Authors: Ruslan Kovtun (shpegun60), codexAi. [MIT](../LICENSE).

Stage 09 supplies the immutable descriptor of a structured `Model`;
Stage 10 exposes it and live values through the existing resource protocol.
Stage 11 adds one-time Bind and complete request/response Exchange. There is no separate `.pri` here or telemetry
dependency in the generic resource core.

```cpp
#include <resource/structured/Descriptor.hpp>

// fields, commands and services are stable structured catalog tables.
inline constexpr telemetry::structured::Model model{fields, commands, services};
inline constexpr resource::structured::Descriptor descriptor{model};

// Optional alternative for immutable compile-time metadata: only the final
// byte array needs to survive linking when the streaming object is unused.
inline constexpr auto descriptorBytes =
    resource::structured::packDescriptor<descriptor>();
```

The model and names are borrowed. Tables and name storage must outlive the
descriptor and remain immutable. Owner state and slot targets may change;
neither is inspected by construction, hashing or reading. A descriptor view
also borrows its owning descriptor; obtaining a view from a temporary is
rejected. Copying a descriptor does not copy or own the underlying tables.

## Format and identity

The [implementation plan, section 10](../../../doc/StructuredTelemetryV3ImplementationPlan.md#descriptor)
is the wire contract. The file contains a 64-byte `TDS3` header, then Type,
Catalog and Field/Command/Service records. Integers are canonical little-endian;
strings are a u32 byte length followed by UTF-8 without NUL. Wire bytes contain
no pointers, C++ padding, unit, limits, defaults, policy flags or type names.
Every endpoint record ends with its name.

The one u64 fingerprint is FNV-1a over the entire descriptor with header
bytes 16–23 treated as zero. It is computed once during construction, or
at compile time for a constexpr object. `read()` and `fingerprint()` never
recalculate it. Field writable capability comes from its definition type,
not the current availability of a setter slot or weak function address.

Changing shapes, dictionaries, positions, names or writable capability changes
the descriptor bytes and normally the fingerprint. Owner/callback addresses,
live values and slot availability do not. A hash collision remains possible;
the fingerprint is not a payload checksum or an access-control mechanism.

## Streaming and storage

```cpp
std::array<std::byte, 128> out;
resource::Cursor cursor = 0;
auto result = descriptor.read(cursor, out);
// Consume out[0 .. result.written), then continue with result.next.
```

Here the opaque resource cursor is a byte offset. Reading may stop inside a
record or UTF-8 string; the reconstructed file is identical to a single read.
At `size()` the result is `Ok`, zero bytes and EOF. Beyond `size()` it is
`InvalidCursor`. An empty buffer before EOF gives `BufferTooSmall`. Errors
preserve the cursor and leave the output untouched.

The streaming descriptor owns a 12-byte index entry per segment plus one
sentinel. A segment is a header, type prefix, member, enum entry, catalog or
endpoint. It locates a resume position by binary search, then emits only
visited segments. Skipping a string prefix does not rescan it. Reading costs
O(log M + emitted bytes + visited segments), with bounded stack and no heap.
Construction validates names, sorts name indexes in place to check uniqueness,
restores positional order, computes offsets and hashes once. No complete
serialized duplicate is created in RAM.

For a constexpr model, **packed bytes in Flash are the recommended storage**:
the [measured fixture](../../../tests/structured/descriptor/README.md) needed
less Flash and fewer cycles than on-demand emission. `packDescriptor` returns
an ordinary constexpr `std::array<std::byte, N>`; it uses the same canonical
emitter as streaming. Use one representation for a provider. The streaming
form remains useful when names/catalog metadata are constructed at startup.
No explicit `init()` phase is required. `DescriptorFile{descriptorBytes}`
wraps packed storage; `DescriptorFile{descriptor}` wraps indexed streaming.
Both borrow a stable immutable lvalue and reject temporary sources.

## Validation and limits

Names are nonempty valid UTF-8. Factory arguments preserve const array extent
long enough to reject embedded NUL in literals; mutable character buffers and
char pointers use bounded C-string semantics. A pointer must reference a valid
NUL-terminated string. Text after its first terminator is not part of that
string. Names are stored as the same single borrowed pointer as before; native
endpoint dispatch gains neither a member nor a runtime name check.

Catalog names are unique within their category; endpoint names are unique
within their catalog. Cross-category duplicates are valid. In a constexpr
descriptor, an invalid declaration produces a construction diagnostic. At
runtime descriptor validation reports `DescriptorError`, size/fingerprint zero
and `InvalidData` on read; invalid factory names themselves remain contract
violations. `Descriptor<F,C,S,Profile>` checks its model against profile ceilings for
type count/depth, members, arrays, enum entries, strings, wire bytes, expanded
nodes, descriptor bytes and total catalogs/endpoints. The independent global
`Type<T>`/registry checks still apply; a profile cannot enable an unsupported
wire type. Profiles normally lower ceilings. The current implementation does
not require every profile constant to be at most its `Limits` counterpart,
so descriptor-only ceilings can also be raised. This Stage 09 review note
records existing behavior; Stage 11 does not change profile validation.

The independent host parser fixture checks header budgets before allocating
record collections, validates every reference and recomputes wire sizes,
depth and expanded nodes without expanding arrays. It is a test oracle;
the application client is scheduled for Stage 12.

## Values and integration

```cpp
#include <resource/structured/DescriptorFile.hpp>
#include <resource/structured/ValuesFile.hpp>
#include <resource/FileSystem.hpp>

namespace ts = telemetry::structured;
namespace rs = resource::structured;
inline constexpr rs::Descriptor descriptor{model};
inline constexpr auto descriptorBytes = rs::packDescriptor<descriptor>();
inline constexpr rs::DescriptorFile descriptorFile{descriptorBytes};

inline std::array<std::byte, model.maxFieldScratch()> storage;
inline ts::Workspace workspace{storage};
inline constexpr rs::ValuesFile values{descriptor, workspace};
constinit auto files = resource::filesystem(
    resource::file("/telemetry3/descriptor.bin", descriptorFile),
    resource::file("/telemetry3/values.bin", values));
```

`ValuesFile` copies the descriptor's field index and cached fingerprint, then
builds one 8-byte offset/packed-ID entry per field plus an EOF sentinel. It
does not borrow the descriptor object itself. The underlying tables/names and
Workspace must outlive all reads; metadata must stay immutable. Construction,
`size()` and STAT never invoke a getter. For constexpr metadata the index and
size are computed at compile time. Both providers are read-only.

The values header is **24 bytes**, all integers little-endian:

| Offset | Type | Meaning |
| --- | --- | --- |
| 0 | bytes[4] | `TVL3` |
| 4 | u16 | major 3 |
| 6 | u16 | minor 0 |
| 8 | u32 | Field count |
| 12 | u32 | Total bytes |
| 16 | u64 | Cached descriptor fingerprint |

The fingerprint identifies the required descriptor. The receiver compares it
once when reading the file header, before interpreting values. It is not a
checksum of changing payloads and is not copied into each resource chunk.
No hash is computed by values READ. Offline exports need both descriptor and
values files. Descriptor wire bytes from Stage 09 have not changed.

Each field contributes `u8 status + wireSize<T> payload` in descriptor order.
Status 0 is Ok; status 1 is Unavailable, with an all-zero payload that must not
be decoded as a value. Other statuses are invalid. Empty structs/arrays still
contribute their status byte. Total size is always `24 + sum(1 + wireSize<T>)`.

READ checks whole-token capacity **before** reserving scratch or resolving a
binding. A fitting token reads its getter once and encodes that snapshot using
the existing typed Field thunk. There is no allocation or intermediate file
copy. This is a snapshot of one returned T, not an atomic snapshot of all fields
or synchronization with a concurrent writer.

Valid cursors are byte positions inside the header, exact token starts and EOF.
Interior payload offsets are rejected before touching getters/output. Resume
uses binary search over offsets and O(1) packed-ID access; it does not replay
earlier getters. A completed prefix is returned as Ok when the next token
does not fit. With no completed bytes, READ returns BufferTooSmall and preserves
the cursor. EOF gives Ok/zero bytes/eof=true without callbacks.

`requiredWorkspace()` includes worst-case alignment margin;
`maxTokenSize()` is the minimum payload capacity to make progress over every
field. The generic protocol's u16 payload ceiling is 65535 bytes. A larger
token returns BufferTooSmall; callers must report this capacity mismatch,
not retry forever. Large-object fragmentation is outside this provider.

Local/Workspace selection is the existing compile-time policy, default 32 B;
it changes storage only, never validation or wire bytes. All-local files never
access Workspace. If a file contains Workspace-backed fields, its output span
must not overlap Workspace storage: InvalidData is returned before even a
header prefix is written. Actual free scratch is checked before a large
getter. Insufficient scratch gives InternalError, not a false Unavailable
value. If earlier tokens were completed, that prefix is returned first;
retrying its nextCursor reports the configuration error without replay.

An invalid Descriptor produces a rejected ValuesFile (`size()==0`,
`fingerprint()==0`, READ InvalidData). Providers are copyable; copies own their
offset indexes but share the borrowed sources/Workspace. Parallel reads require
external serialization or separate provider/Workspace instances. Leases keep
the documented LIFO lifetime contract.

## Bind and Exchange

```cpp
#include <resource/structured/Bind.hpp>
#include <resource/structured/Exchange.hpp>

namespace rs = resource::structured;
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

The [plan, section 12](../../../doc/StructuredTelemetryV3ImplementationPlan.md#12-запис-команди-та-rpc-через-транспорт)
defines the exact 24-byte `TSRQ`/`TSRP` envelopes. Exchange request offsets:

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

The [bounded transport fixture](../../../tests/structured/exchange/FakeTransport.hpp)
demonstrates these integration responsibilities with two peers, separate
Workspaces, fixed queues and a bounded client correlation table. Its generation
is fake connection metadata, never a new telemetry wire field. A real UART/TCP
adapter supplies framing/reassembly and its own admission/disconnect mechanism.

## qmake

Select optional dependencies explicitly, before including resource.pri:

```qmake
CONFIG += resource_structured
include(path/to/lib/telemetry_structured/structured.pri)
include(path/to/lib/resource/resource.pri)
```

For v2 adapters also select `resource_telemetry` and explicitly include
`telemetry/telemetry.pri` first. `telemetry_no_json` remains supported. There are
no reverse includes between these .pri files. Repeated structured/resource
includes are guarded. The v3 compiled reader is `detail/Values.cpp`, avoiding
an object-file basename collision with v2 `telemetry/ValuesFile.cpp` in qmake.
Core-only builds have no PFR dependency. The
[four-mode integration check](../../../tests/structured/resources/resources.pro)
and [DeviceResources example](../../../tests/structured/resources/DeviceResources.cpp)
exercise this composition and expose only `resource::FileSystemView` publicly.
