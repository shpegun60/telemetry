# Structured telemetry (under construction)

Authors: Ruslan Kovtun (shpegun60), codexAi. Own code: MIT.

The new C++20 module follows the
[structured v3 implementation plan](../../doc/StructuredTelemetryV3ImplementationPlan.md).
`telemetry_structured` is its **temporary development location**, not a
permanent second public telemetry library. Through Stage 15 the existing
C++17 scalar core and wire v2.1 remain frozen for comparison. Stages 16–20
move this typed core into `lib/telemetry`, migrate consumers, and remove the
old `Scalar` core, v2.1 adapter, and legacy JSON adapter. The final library
has one `telemetry` namespace (`ts` may be a user alias), one mixed
Field/Command/Service model, and wire v3 only. This migration has **not**
happened yet.

Its current implementation includes the stable
[reflection facade](reflection/Reflection.hpp),
[fixed wire type traits](type/Traits.hpp), the
[compile-time TypeRegistry](type/Registry.hpp), and the
[canonical codec](codec/Codec.hpp) with caller-owned
[Workspace](codec/Workspace.hpp), native
[Field](field/Field.hpp), [Command](command/Command.hpp) and
[Service](service/Service.hpp) bindings, and the Stage 08 mixed
[Model](model/Model.hpp). The facade supplies aggregate and callable
facts plus a normalized enum dictionary. The traits classify supported types
and calculate wire size, nesting depth and expanded value nodes during
compilation. The registry deduplicates exact C++ types, registers nested
dependencies first, and exposes immutable structural descriptors. The
Model offers local/global native operations and bounded encoded runtime
operations for all three families. Canonical descriptor/values files are in
[`resource/structured`](../resource/structured/README.md). Connections, peers,
session state, packet framing, correlation and agreement policy belong to
the application. The optional
[protocol example](../../examples/structured_protocol/README.md) is selected
explicitly; neither library includes it.

```cpp
struct Reading { float volts; std::uint16_t status; };
using Types = telemetry::structured::TypeRegistry<Reading>;

static_assert(Types::typeId<float>() == 10);   // Fixed built-in TypeId.
static_assert(Types::typeId<Reading>() == 12); // First user type.
constexpr auto types = Types::view();
```

The standalone TypeRegistry example lists roots explicitly. A Model instead
derives them from its tables in Field, Command, then Service order, with each
Service request preceding its response. Empty catalog tables are valid;
`emptyFields`/`emptyCommands` also remain available for Service-only models.
`TypeRegistryView::find(id)` checks
bounds; `TypeRegistry::descriptor<Id>()` requires a known valid ID at compile
time. `recordsBytes` counts type records only, including their record headers;
the Model will check the size of the entire descriptor. Member and enum names
are borrowed immutable metadata, so their backing storage must outlive the
registry view. There are no units, limits, defaults, owner addresses or live
values in these descriptors. Stage 09 encodes `descriptor.bin` and its cached
fingerprint; Stage 10 copies that fingerprint into the values file header.

Modules exchanging in-memory descriptor views can call
[`requireStructuredAbi()`](abi/StructuredAbi.hpp) at their boundary and link
`abi/StructuredAbi.cpp`. The compiled
[`callServiceEncoded()`](model/Adapter.hpp) carries the same exact layout tag
in its symbol, so that adapter boundary checks it automatically at link time.
Neither mechanism runs during typed value access. Other header-only module
boundaries still need an explicit ABI dependency.
Applications using the compiled adapter link `model/Adapter.cpp` and
`abi/StructuredAbi.cpp` once; local/global native calls need neither object.
The tag detects revision, size and offset mismatches. A change in the meaning
of a view with unchanged layout requires an explicit `structuredAbiRevision`
bump; the linker cannot infer semantics from equal bytes.

The payload has exact `wireSize<T>` bytes: scalar little-endian bytes, enum
underlying code, arrays in element order, and aggregates in member order.
There are no C++ padding bytes, tags, or per-value lengths. A decoder first
checks the exact length and every `bool` byte (only 0/1), then constructs a
fully initialized object in the caller's storage. Unknown enum codes remain
valid values of the underlying type. A malformed payload never yields an
object to the caller.

```cpp
Reading reading{230.0f, 1};

std::array<std::byte, telemetry::structured::wireSize<Reading>> wire{};
auto encoded = telemetry::structured::encode(reading, wire);

alignas(Reading) std::array<std::byte, sizeof(Reading)> scratch{};
telemetry::structured::Workspace workspace{scratch};
auto lease = workspace.reserve<Reading>();
Reading* decoded = nullptr;
auto result = telemetry::structured::decode<Reading>(wire, lease, decoded);
if (result == telemetry::structured::CodecStatus::Ok) {
    // decoded remains valid until lease is destroyed.
}
```

An unaligned caller buffer can use `scratchBytes<T>` as a sufficient
single-object bound. Live leases must be destroyed in reverse order; Request
and Response may have separate simultaneous leases in one Workspace. The
standalone codec rejects overlap between its wire bytes and native object.
Encoded endpoints require wire buffers to be disjoint from Workspace only
when the endpoint actually uses scratch. Service input and output may share
bytes: decoding completes before response encoding starts. The caller synchronizes
access to a shared Workspace and owns the byte buffer for its entire lifetime.

[ServiceResult<T>](result/ServiceResult.hpp) supplies status and optional
payload lifetime for native Service calls. For a large
response, use `successFrom([] { return responseValue(); })`: its prvalue is
constructed in the result's destination storage. The convenience
`success(value)` is limited to 256-byte payload objects, because accepting a
4 KiB value by parameter produced an 8 KiB stack frame on the tested ARM
compiler. Stage 06 rechecked the real Service wrapper with a 4 KiB response;
its measured individual ARM frames are in the test README.

`service<target>(name[, owner])` binds a known function or method;
`service(name, callable)` accepts a native function pointer, a capture-free
lambda, a stable capturing callable lvalue, or one of the existing slot
families. The callback signature infers the aggregate Request and Response.
There are no Service limits, units, argument labels or other semantic
metadata. A raw `Response`/`void` return becomes `ServiceResult<Response>`;
an exact `ServiceResult<Response>` passes through unchanged. A missing
runtime function, empty slot or absent weak target returns `Unavailable`
without invoking application code. A native callback may return the same
status itself, so native callers do not distinguish those two sources.
Borrowed owners, callables, slots, names and their transitive references must
outlive the Service. Rebinding a slot requires external synchronization with
calls. Direct owner bindings have no owner-null check; an `OwnerSlot` is
checked before invocation. The table stores its definitions and erased runtime
entries together, so it cannot be copied or moved; catalogs borrow stable
table addresses and likewise cannot be copied or moved.

```cpp
struct ReadCalibrationRequest { std::uint16_t channel; };
struct ReadCalibrationResponse { std::uint32_t scale; };

inline Device device;
inline constexpr telemetry::structured::ServiceTable local{
    telemetry::structured::service<&Device::readCalibration>("ReadCalibration", device)
};
inline constexpr telemetry::structured::ServiceCatalogTable services{
    telemetry::structured::group("device", local)
};
inline constexpr telemetry::structured::Model model{
    telemetry::structured::emptyFields,
    telemetry::structured::emptyCommands, services
};

auto native = local.call<0>(ReadCalibrationRequest{1});
auto global = services.call<telemetry::makeId<0, 0>()>(ReadCalibrationRequest{1});
auto encoded = model.serviceIndex().callEncoded(
    telemetry::makeId<0, 0>(), inputBytes, outputBytes, workspace);
auto viaAdapter = telemetry::structured::callServiceEncoded(
    model.view(), telemetry::makeId<0, 0>(), inputBytes, outputBytes, workspace);
```

The compiled adapter takes a 32-bit `PackedId`, the same ID type used by the
telemetry catalogs. Decode an incoming wire ID as `u32` before calling it.

The native calls retain the exact request/response types. The encoded call
returns `EncodedCallResult`: `dispatch` reports lookup, payload, buffer,
workspace and missing-target failures; `endpointStatus` reports the status
returned by an invoked Service. Thus a missing target yields dispatch
`Unavailable`, while an application-returned `ServiceStatus::Unavailable`
yields dispatch `Ok`. The response payload is written only for a successful
endpoint result. Before any callback, the runtime route checks the ID, exact
request length, capacity for the full successful response, any required
scratch capacity/aliasing and every encoded bool. It snapshots a slot
once. `model.maxServiceScratch()` includes the simultaneously live Request and
`ServiceResult<Response>` objects assigned to Workspace, plus alignment margin;
`model.maxServiceResponseWireSize()` reports the largest response payload.
These are sufficient bounds, so an already aligned buffer can sometimes work
with fewer bytes. The Service names and borrowed bindings must outlive every
table/catalog/model view.

## Mixed fields and commands

`field<getter[, setter]>(name[, owner])` binds known targets. The parameter
form is `field(name, getter[, setter])`. As with Service, it accepts ordinary
functions, capture-free lambdas, stable capturing callable lvalues and slots.
Keep both callbacks in the template form or both in the parameter form.
`command<target>(name[, owner])` and `command(name, callable)` use the same
binding rules. Shared storage/snapshot rules live in
[`detail/Binding.hpp`](detail/Binding.hpp); each family owns its signature
validation, native results, table and catalog headers in its own directory.

A Field getter returns an unqualified supported native `T` by value. Its
setter takes exactly `T` or `const T&` and returns `telemetry::WriteResult`.
A Command takes zero arguments or one aggregate Request, by value or const
reference, and returns exactly `telemetry::CommandResult`. All callbacks
must be `noexcept`. Field scalar types remain native scalar types: there is
no `Scalar` construction or implicit numeric coercion in ordinary exact
`read/write`. Explicit `readAs/writeAs` add checked native numeric conversion
without Scalar. Validation of application values belongs to the setter/command.
There are no units, limits, defaults or argument annotations.

```cpp
namespace ts = telemetry::structured;
struct Config { float target; std::uint16_t rpm; bool enabled; };

// Assume device supplies the shown noexcept getters/methods.
inline constexpr ts::FieldTable localFields{
    ts::field<&Device::readRpm>("RPM", device),              // uint16_t
    ts::field<&Device::readConfig, &Device::writeConfig>("Config", device)
};
inline constexpr ts::CommandTable localCommands{
    ts::command<&Device::configure>("Configure", device)   // CommandResult(const Config&)
};
inline constexpr ts::FieldCatalogTable fields{ts::group("motor", localFields)};
inline constexpr ts::CommandCatalogTable commands{ts::group("motor", localCommands)};
inline constexpr ts::ServiceCatalogTable services{};
inline constexpr ts::Model model{fields, commands, services};

auto rpm = localFields.read<0>();                         // optional<uint16_t>
auto config = fields.read<telemetry::makeId<0, 1>()>();    // optional<Config>
auto changed = localFields.write<1>(Config{230.f, 1500, true});
auto result = commands.call<telemetry::makeId<0, 0>()>(Config{250.f, 1800, true});

std::array<std::byte, model.maxScratch()> storage{};
ts::Workspace workspace{storage};
auto read = ts::readFieldEncoded(model.view(), fieldId, outputBytes, workspace);
auto write = ts::writeFieldEncoded(model.view(), fieldId, inputBytes, workspace);
auto call = ts::executeCommandEncoded(model.view(), commandId, inputBytes, workspace);
```

Local template positions accept integers or enums. Global IDs are packed
`u32` group/position pairs in separate Field, Command and Service spaces.
All families share one Registry; the exact same `Config` used by all three
has one TypeId. `model.view().fieldTypeId(id)`, `commandTypeId(id)` and
`serviceTypeIds(id)` expose those references. `maxScratch()` is the largest
single-operation bound across the three categories; it is not a budget for
nested or concurrent operations. Such operations need separate scratch or
enough remaining space for every live lease.

Native Field reads return `optional<T>`; absent targets return `nullopt`.
An absent setter capability returns `WriteResult::ReadOnly`; an empty bound
setter returns `WriteResult::Unavailable`. Encoded results separate dispatch
failures from application status, exactly as Service does. **Read
`endpointStatus` only when `dispatch == Ok`.** Unknown application status
codes become `InternalError` at the encoded boundary. `Accepted` is simply
returned; the library does not create a queue or retain the decoded request.

Encoded writes/commands validate exact payload length, scratch aliasing
and bool representations before resolving the target. The compile-time
`TELEMETRY_STRUCTURED_LOCAL_BYTES` budget defaults to **32 bytes**. A Field's
native value or a Command's request uses a local object when its `sizeof(T)`
fits that budget; larger objects use caller-owned Workspace. Zero forces
Workspace for all payload objects, including scalar Fields. There is no
runtime size branch and no change to codec bytes or the native typed API.
The threshold is **only a storage policy**. It never changes supported-type
rules, exact-length/representation validation or wire semantics. A small
aggregate still follows the aggregate contract, including alignment requirements
and DMI-neutral construction; it is not treated as an unchecked scalar.
Only the scratch requirement and the resulting WorkspaceTooSmall/alias checks
depend on whether the selected endpoint actually uses Workspace.

Service gives its Request first use of this budget. The actual
`ServiceResult<Response>` wrapper is local only if it fits the remainder.
Request and Result may therefore use different storage. Only objects assigned
to Workspace contribute to the Model's scratch requirements. The void-status
result remains an ordinary status, without a response payload allocation.

Define the budget identically in all translation units, for example
`-DTELEMETRY_STRUCTURED_LOCAL_BYTES=64`. It is part of the compiled adapter's
exact ABI tag; a caller and adapter compiled with different budgets fail to
link. This is a budget for live **payload object sizes**, not a total stack
limit: alignment, saved registers, codec helpers and user callbacks also
consume stack. The measured 0/16/32/64 trade-offs are recorded in the
[storage report](../../tests/structured/endpoints/h7s/STORAGE_RESULTS.md).

For a fully local endpoint, Workspace is not accessed, including its buffer
address, capacity or used count. A caller may therefore use an empty Workspace
or let its storage overlap wire bytes. When scratch is required, wire buffers
must remain disjoint from the full Workspace span, including active outer leases.
Service input/output may overlap each other with either storage policy. Its
Request is fully decoded into independent native storage before the callback
and before any response byte is written. An application failure writes no payload.

The checked runtime boundary is also available on a resolved entry as
`FieldEntry::readEncoded/writeEncoded`, `CommandEntry::executeEncoded` and
`ServiceEntry::callEncoded`. Index dispatch calls these after O(1) bounds-checked
array access. Their internal function pointers require prevalidated byte
buffers; they take raw pointers, with exact extents known by the generated thunk.
This is not an additional unchecked public routing API.
Method/callable/slot entries point directly to their binding state. Runtime
function pointers and custom bindings retain a table-owned binding object;
function pointers are never converted to object pointers. A Field stores
separate read/write contexts because they may refer to different objects.

Trivially default-constructible decoded objects are default-initialized
without zeroing; the decoder fills every semantic member before use. Types
with DMI use the same neutral construction as Workspace decoding, without
executing application initializers. Padding is never serialized.
Encoded reads check output capacity and any required scratch first, then call
the getter once. Every operation releases its acquired scratch leases. A
read-only Field is identified before payload checks and returns
`{Ok, ReadOnly}`. Buffer and dispatch failures do not invoke application
callbacks. Getters construct large values directly in Workspace; setters and
commands with `const T&` consume the decoded object there. Choosing a by-value
large request can still incur the copy required by that C++ signature.

Endpoint preflight avoids repeating the standalone codec's same span checks;
it does not bypass bool validation or object construction. Integer leaf
encoding uses `memcpy` on little-endian targets and byte assembly otherwise.
No packed-object reference or raw aggregate copy is used. Normal target flags
govern whether the compiler may use unaligned load/store instructions.

Tables borrow owners, callables, slots and names. They own their definition
objects and runtime entries, so they cannot be moved/copied after their
self-references are formed. Catalogs borrow stable tables. Typed routing
bypasses erased entries when the compiler knows the target; runtime encoded
routing uses direct bounds-checked array indexing, then the compact entries.
`id >> 16` selects the catalog and `id & 0xffff` selects its entry; there is
no traversal, name comparison or hash lookup. On ARM these entries occupy
28/20/24 bytes for Field/Command/Service, with natural alignment. These are
**not total per-endpoint storage sizes**: definitions and structural Registry
metadata also occupy storage. The
[earlier H7S alignment measurements](../../tests/structured/endpoints/h7s/README.md)
did not justify uniform cache-line alignment for the former 24-byte Field
entry. The current 28-byte direct-context layout retains natural alignment;
its [dispatch measurements](../../tests/structured/endpoints/h7s/DISPATCH_RESULTS.md)
do not repeat the 8/32-byte alignment comparison.

A type with default member initializers is constructed with every member
explicitly supplied before decoding. This avoids running a DMI as a hidden
fallback. Because C++20 aggregate initialization must name each nontrivial
array element, the codec caps that compile-time expansion at 1024 nodes and
diagnoses larger cases. Large arrays of trivially default-constructible
elements still decode through a compact loop, including the 4 KiB probe.

## Exact types and convenient runtime access

All three families use the same four access modes:

| Mode | Local table | Global catalog table |
| --- | --- | --- |
| Compile-time exact operation | `read<I>()`, `write<I>(value)`, `call<I>(request)` | Same operations with packed ID |
| Typed definition / traversal | `get<I>()`, `forEach(visitor)` | `get<Id>()`, grouped `forEach(visitor)` |
| Runtime native selection | `visit(position, visitor)` | `visit(id, visitor)` |
| Runtime wire operation | Checked methods on erased entries | `index().readEncoded/writeEncoded/executeEncoded/callEncoded` |

`get` returns the original **const definition reference**, not an erased
entry or a copied value. Local template positions may be scoped enums;
global IDs must be integers packed with `makeId`. Typed traversal preserves
declaration order and skips empty groups:

```cpp
localFields.forEach([]<std::size_t I>(const auto& endpoint) {
    using Value = typename std::remove_cvref_t<decltype(endpoint)>::Value;
    // I is local; no getter has been called by traversal.
});
fields.forEach([]<std::size_t Group, std::size_t Entry>(
    std::string_view groupName, const auto& endpoint) {
    // Group/Entry are compile-time positions; groupName is borrowed metadata.
});
commands.forEach([]<std::size_t Group, std::size_t Entry>(
    std::string_view groupName, const auto& endpoint) {
    using Request = typename std::remove_cvref_t<decltype(endpoint)>::Request;
});
services.visit(serviceId, [](const auto& endpoint) {
    using Service = std::remove_cvref_t<decltype(endpoint)>;
    using Request = typename Service::Request;
    using Response = typename Service::Response;
    // Only the selected exact definition is passed here.
});
```

A local `forEach` also accepts an ordinary generic callback without `<I>`;
a global callback without indices takes `(groupName, endpoint)`. Visitors
are invoked as lvalues, never copied, stored or called asynchronously.
Callback results are ignored; `forEach` visits every entry. `visit` returns
true for one selected endpoint and false for an invalid ID, without invoking
the callback. This bool is **not** target availability or application status.
Inspect the endpoint's native result for that. Callback exceptions propagate
when exceptions are enabled; library callbacks themselves remain noexcept.

Runtime visitors use a static dispatch table specialized for the visitor
type. Global dispatch selects group, then row: O(1) indexed selection with
bounds checked at the original ID width. No linear scan, variant, live Value
construction or wire encoding is involved. Each used visitor specialization
has a code/Flash cost; the [ARM measurements](../../tests/structured/traversal/README.md)
record it rather than treating this native convenience as free runtime dispatch.

Fields also provide direct native runtime access without a user visitor:

```cpp
auto rpm = fields.readAs<float>(fieldId);       // optional<float>
auto status = fields.writeAs(fieldId, 1500.5);  // WriteResult

// Local runtime forms use local position, never a packed global ID.
auto local = localFields.readAs<double>(position);
localFields.writeAs(position, value);

// Compile-time forms keep the endpoint known to the compiler.
auto precise = fields.readAs<double, telemetry::makeId<0, 0>()>();
fields.writeAs<telemetry::makeId<0, 0>()>(value);
localFields.get<0>().readAs<double>();
```

`readAs` returns nullopt for NotFound, an unavailable getter, an incompatible
structural type or an unrepresentable numeric conversion. `writeAs` returns
NotFound for an invalid position, ReadOnly before conversion when no setter
exists, InvalidValue for mismatch/conversion failure, or the native setter
status (including Unavailable). Failed conversion never calls the setter;
structural mismatch never calls the getter. No hidden encode/decode occurs.

Conversion shares the existing native checked-number policy: float to integer
truncates toward zero with checked bounds; integer bounds use integer arithmetic.
Floating targets accept NaN/Inf and may round; finite narrowing overflow fails.
Bool uses finite zero/nonzero. Enum conversion uses the underlying integer;
unknown representable codes are accepted, matching the codec. Structs and arrays
require the exact C++ type. Static structural mismatch is a compile-time error;
runtime mismatch is nullopt/InvalidValue. Ordinary read/write remain exact.

`readAs<T>` explicitly returns an owning optional<T>, just like native read.
A caller asking for a 4 KiB T owns that large result and its native return ABI.
For bounded large-object storage use encoded access with caller-owned Workspace.
Traversal itself does not read, copy or allocate that 4 KiB object. The 32-byte
storage budget affects encoded endpoints only, not native value returns.

All tables/catalogs also have `empty/begin/end/operator[]`. Range iteration
borrows homogeneous entries/catalogs; `operator[]` has the usual unchecked
container contract. `get`, iteration, traversal and As access reject rvalue
tables/catalogs so borrowed addresses cannot escape a temporary. Owners,
slots, names and definitions must still outlive their borrowers.

For an ordinary scoped enum, names and codes are inferred automatically:

```cpp
enum class Mode : std::uint8_t { Off, Auto, Manual };

static_assert(telemetry::structured::reflection::Enum<Mode>::entryCount == 3);
static_assert(telemetry::structured::reflection::Enum<Mode>::entryName<1>() == "Auto");
```

An explicit dictionary replaces the automatic one. `enumCodes` asks the
backend for names of chosen values, including sparse values outside its
automatic scan. `enumEntries` copies exact UTF-8 names into the constexpr
definition; it does not borrow a temporary string:

```cpp
enum class Error : std::uint16_t { None = 0, Far = 1000 };

template <>
struct telemetry::structured::reflection::EnumReflection<Error> {
    inline static constexpr auto entries =
        telemetry::structured::reflection::enumEntries(
            telemetry::structured::reflection::enumEntry(Error::Far, "Far"),
            telemetry::structured::reflection::enumEntry(Error::None, "None"));
};

static_assert(telemetry::structured::reflection::Enum<Error>::entryValue<0>() ==
              Error::None);
```

`wireSize<T>` excludes C++ object padding. For example, a normal aggregate
with `float` and `std::uint16_t` members has a six-byte wire payload. There is
no reason to mark the C++ object `packed` to make its wire representation
compact. Packed objects are outside the supported aggregate contract because
PFR may expose a misaligned member through an ordinary reference. The traits
reject known unaligned forms, but C++20 reflection cannot diagnose every
compiler-specific packing attribute; callers must supply ordinary aligned
aggregates. Copy an external packed object into such a value before using this
API.

Only the files under `reflection/detail` include the pinned
[Boost.PFR source](../boost_pfr/VERSION.md) and `magic_enum`. Current C++20
consumers need the include directories `lib`, `lib/boost_pfr/include` and
`lib/magic_enum`. The existing scalar telemetry library does not depend on
this module. Focused verification and the exact compiler matrix are in
[tests/structured](../../tests/structured/README.md).
