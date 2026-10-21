# Runtime schema and dynamic wire values

The C++20 runtime reader consumes Descriptor v3.0 bytes without knowing a
Model's native C++ types. `RuntimeSchema` indexes validated types, structure
members, enum entries, catalogs and endpoints in caller-owned storage.
`DynamicValueView` borrows canonical payload bytes and reads scalar leaves;
it does not construct a native aggregate or a tree of values.

The reader lives in `telemetry::runtime`. It uses the same fixed wire kinds,
limits and descriptor constants as the producer, without native reflection,
Resource, Client, Integration, Qt or an operating system. Writer and dynamic
request scheduling are separate future work. Existing dispatchers, clients,
Descriptor v3.0, TEQ1 and RPC wire contracts are unchanged.

## Read an unknown value

Include `<telemetry/runtime/DynamicValueView.hpp>`. The schema-only header is
`<telemetry/runtime/RuntimeSchema.hpp>`. Supply arrays sized for the application's
schema budget; parsing never grows them or allocates hidden storage.

```cpp
namespace rt = telemetry::runtime;
std::array<rt::RuntimeType, 64> types{};
std::array<rt::RuntimeMember, 256> members{};
std::array<rt::RuntimeEnumEntry, 128> enums{};
std::array<rt::RuntimeCatalog, 32> catalogs{};
std::array<rt::RuntimeEndpoint, 128> endpoints{};
rt::RuntimeSchemaWorkspace workspace{types, members, enums, catalogs, endpoints};

auto parsed = rt::RuntimeSchema::parse(descriptorBytes, workspace);
if (!parsed) {
    // Inspect parsed.status, parsed.error and parsed.requirements.
    return;
}
const auto* field = parsed.schema.findField("device", "Config");
if (!field)
    return;
auto handle = parsed.schema.resolve(field->typeId, "baudRate");
auto value = parsed.schema.view(field->typeId, responseBytes);
if (!handle || !value)
    return;
auto baud = value.value.get<std::uint32_t>(handle.value);
if (!baud)
    return;
useBaudRate(baud.value);
```

The example assumes the descriptor describes that field/member as a `U32`.
Each lookup/read reports failure explicitly if the peer uses another schema.
The handle can be reused for subsequent payloads of the same root type in the
same schema generation. `member(name)`, `member(index)` and `index(element)`
return checked subtree views for exploratory access. `get<T>()` reads an exact
scalar leaf. `enumUnderlying<T>()` reads an enum's exact integer type;
`enumName()` returns `{known, name}` within a checked result.

Paths support nested members and array indexes, such as `motor.samples[2]`.
An empty path selects the root. `resolve(typeId, span<const PathStep>)` provides
explicit member/index steps when a member name contains path punctuation.
Catalog and endpoint enumeration includes Fields, Commands and Services; this
stage supplies metadata and value reads, without dynamic request submission.

## Storage and lifetime

The application owns these storage lifetimes:

| Storage | Requirement |
| --- | --- |
| Descriptor bytes | Remain alive and immutable while schema metadata or names are used |
| Workspace object | Remains alive at a stable address until all schemas, handles and views are gone |
| Index arrays | Remain alive and immutable for their published generation; writable again before another parse |
| Payload bytes | Remain alive and immutable while a value view is used |

Metadata names are `std::string_view` slices of the original descriptor.
Wire offsets exclude C++ object padding and native alignment. Scalar reads
permit unaligned payloads and decode canonical little-endian bits.

Resetting a workspace or attempting a new parse invalidates old schemas,
value views and prepared handles. This generation check requires the workspace
object itself to remain alive; it cannot detect an object that was already
destroyed or storage that the application changed behind a borrowed span.
Invalidate views before replacing buffers, and do not access stale metadata
pointers. Parsing and reset require external synchronization; simultaneous
read-only access is allowed while all borrowed storage remains immutable.

Schemas, parse results, views and handles can be copied or moved. They retain
the workspace identity and generation by value; they do not borrow the original
schema or parse-result object. These operations neither transfer storage
ownership nor extend the lifetime of descriptor, workspace or payload buffers.

A prepared handle identifies its workspace, generation, root type, leaf type
and offset. Reading with a handle checks its binding and bounds without another
member-name lookup or structural traversal. A handle for one root cannot be
applied to a different root or workspace, even when their type IDs coincide.

## Validation and value semantics

Descriptor validation covers the fingerprint, strict header/version/flags,
section and record bounds, reserved bytes, exact record exhaustion, positional
IDs, canonical built-in types, backward type references, checked wire sizes,
nesting and expanded-node budgets, UTF-8 strings, unique names within their
scopes, sorted enum codes, catalog coverage and endpoint type/capability rules.
Workspace capacity does not make an unchecked descriptor trusted.

A value view requires the exact wire size and validates Boolean representations
once when the root view is created. Subtree navigation checks metadata and
bounds without rescanning the payload. Prepared scalar reads access only the
selected leaf after checking the schema generation and handle identity.
Scalars require their exact wire type; a request for
a different C++ scalar reports a type mismatch rather than a numeric conversion.
Floating-point values preserve signed zero, infinities and NaN bit patterns.
Enums remain open, matching the existing codec: unnamed underlying integer
codes are valid values and report an unknown label.

## Build

```sh
cmake -S . -B /tmp/telemetry-runtime -DTELEMETRY_COMPONENTS=runtime \
  -DCMAKE_INSTALL_PREFIX=/tmp/telemetry-runtime-install
cmake --install /tmp/telemetry-runtime
```

```cmake
find_package(Telemetry REQUIRED COMPONENTS runtime)
target_link_libraries(application PRIVATE Telemetry::runtime)
```

```qmake
include(path/to/telemetry/lib/telemetry/runtime/runtime.pri)
```

## Verification

[The runtime tests](../tests/runtime_schema/README.md) exercise a native Host
Model, its real descriptor and encoded response, the generated standalone C++
codec, runtime metadata and dynamic value reads. Their deterministic malformed
corpus is checked against the production Python descriptor parser.

```sh
python3 -B tests/runtime_schema/run.py --cxx g++ \
  --build-dir /tmp/runtime-schema-gcc
python3 -B tests/runtime_schema/run.py --cxx clang++-18 --sanitize \
  --build-dir /tmp/runtime-schema-sanitized
```

The installed-module suite checks relocation and negative includes for the
standalone runtime component and compiles/links it for Cortex-M7 without
executing a board image. These checks establish software behavior only.
