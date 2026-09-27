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
[Service bindings](model/Service.hpp), and the Stage 07
[ServiceTable and Model](model/Model.hpp). The facade supplies aggregate and callable
facts plus a normalized enum dictionary. The traits classify supported types
and calculate wire size, nesting depth and expanded value nodes during
compilation. The registry deduplicates exact C++ types, registers nested
dependencies first, and exposes immutable structural descriptors. The
Service-only Model now offers local/global native calls and a bounded encoded
call. Fields, Commands, descriptor bytes and transport remain later stages.

```cpp
struct Reading { float volts; std::uint16_t status; };
using Types = telemetry::structured::TypeRegistry<Reading>;

static_assert(Types::typeId<float>() == 10);   // Fixed built-in TypeId.
static_assert(Types::typeId<Reading>() == 12); // First user type.
constexpr auto types = Types::view();
```

The standalone TypeRegistry example lists roots explicitly. A Model instead
derives them from its tables in Field, Command, then Service order, with each
Service request preceding its response. Stage 07 uses empty Field/Command
catalog placeholders until Stage 08. `TypeRegistryView::find(id)` checks
bounds; `TypeRegistry::descriptor<Id>()` requires a known valid ID at compile
time. `recordsBytes` counts type records only, including their record headers;
the Model will check the size of the entire descriptor. Member and enum names
are borrowed immutable metadata, so their backing storage must outlive the
registry view. There are no units, limits, defaults, owner addresses or live
values in these descriptors. Stage 09 will encode the final `descriptor.bin`.

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
and Response may have separate simultaneous leases in one Workspace. Input,
output, and workspace byte ranges must not overlap. The caller synchronizes
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
request length, capacity for the full successful response, disjoint buffers,
caller-owned scratch capacity and every encoded bool. It snapshots a slot
once. `model.maxServiceScratch()` includes simultaneously live Request and
`ServiceResult<Response>` storage plus alignment margin;
`model.maxServiceResponseWireSize()` reports the largest response payload.
These are sufficient bounds, so an already aligned buffer can sometimes work
with fewer bytes. The Service names and borrowed bindings must outlive every
table/catalog/model view.

A type with default member initializers is constructed with every member
explicitly supplied before decoding. This avoids running a DMI as a hidden
fallback. Because C++20 aggregate initialization must name each nontrivial
array element, the codec caps that compile-time expansion at 1024 nodes and
diagnoses larger cases. Large arrays of trivially default-constructible
elements still decode through a compact loop, including the 4 KiB probe.

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
