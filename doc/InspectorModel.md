# Read-only Inspector Model

The optional Integration component `inspector` connects a parsed Descriptor v3
to a read-only browser without Qt, generated DTOs or a new transport. Its public
header is `integration/inspector/InspectorModel.hpp`, in namespace
`telemetry::integration::inspector`.

It lists Field, Command and Service catalogs and endpoints. Only Field reads
can be submitted. Commands and Services are metadata; this version provides no
write or execution controls. A private DynamicClient owns the existing bounded
FIFO and uses an application-supplied Encoded, Retrying Encoded or RPC backend.

## Ownership

The application obtains Descriptor bytes from a file or its existing Resource
path, parses them with RuntimeSchema, then admits that schema and the observed
descriptor header for one session. Descriptor fetching is outside DynamicClient
and Inspector Model. The [runtime storage contract](RuntimeSchema.md) still
applies: the workspace object, descriptor and index arrays are caller-owned.
The workspace control object must outlive the model and its handles. Keep
published Descriptor/index storage immutable and alive until invalidation and
proper drain. Invalidate the workspace before releasing its backing arrays;
another parse still needs writable index storage.

The model owns a bounded wire snapshot of the selected Field. It copies response
bytes before the DynamicClient callback returns and returns copied completion
metadata to the application callback. No borrowed ResultView payload is retained. The snapshot
does not require a heap allocation or an expanded tree of DTO objects.

Nodes are lazy handles tied to the model and snapshot revision. A refreshed or
cleared snapshot invalidates old handles even when the schema has not changed.
Handle checks precede access to stored value views or metadata. The public API
does not expose a raw DynamicValueView into a reusable snapshot buffer.
Catalog/endpoint spans, node names and optional enum labels still borrow the
published schema generation. Copy labels into application-owned text before
retaining them across schema replacement; numeric scalar values are copies.

Scalar reads return exact values. Decimal presentation preserves all U64/S64
digits without passing through double or a JavaScript Number. An open enum with
an unknown code remains a valid value and is presented with its numeric code.
Nested structures, arrays, empty structures and zero-length arrays retain their
actual type and shape; a zero-byte payload is not automatically a missing value.

Select an endpoint with `bindField(id)`, then call `readField(binding, callback)`
or `selectField(binding)` followed by `refresh(callback)`. An accepted read
retires the previous snapshot; a refused read preserves it while the schema and
session remain current. A response larger
than `SnapshotBytes` is refused before enqueue with client `BufferTooSmall` and
model `SnapshotTooLarge`. Queue refusal is the existing `QueueFull`, with no
hidden pending queue in the inspector.

The callback receives a copied `ReadResult`: operation status, dispatch/endpoint
details, runtime value status and model publication status remain distinct. A
superseded read still completes once, but `published` is false and its
`modelStatus` is `Stale`. `snapshotInfo()` identifies the selected Field,
admission and snapshot revision. `root()`, `childCount()`, `child()` and `info()`
navigate lazily; `scalar()` and `decimal(node, output)` return exact scalar
copies and integral text. There is no whole-value tree allocation.

## Session changes and shutdown

One owner serializes the model, schema, backend and transport port. Invalidate
before replacing a Descriptor or session. Invalidation clears selected values
and invalidates node handles; stale completions cannot republish old bytes.
External backend session changes and workspace invalidation are checked before
metadata or values are exposed.

Continue owner processing to drain accepted work. Before admitting another
session, supply the existing DynamicClient quiescence proof only after physical
TX ownership is released, old frames are excluded and the old remote session is
closed, including any partial RPC transaction. An empty FIFO alone is not that
proof. Close and drain before destroying the model. See the
[DynamicClient lifecycle](DynamicClientUsage.md).

## Build and example

```cmake
find_package(Telemetry REQUIRED COMPONENTS inspector)
target_link_libraries(application PRIVATE Telemetry::inspector)
```

Select a concrete backend separately. With qmake, include
`lib/integration/inspector/inspector.pri`; it selects only its runtime/client
dependencies and does not add Qt modules.

The [host example](../examples/inspector/README.md) uses a real local Model and
Encoded dispatcher. Its values are demonstration data, not live board readings.
The [host suite](../tests/inspector/README.md) checks the same model contracts
through Encoded and RPC. Qt/QML presentation and physical STM32 transport
qualification are separate later stages.
