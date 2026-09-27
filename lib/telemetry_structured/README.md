# Structured telemetry (under construction)

Authors: Ruslan Kovtun (shpegun60), codexAi. Own code: MIT.

The new C++20 module follows the
[structured v3 implementation plan](../../doc/StructuredTelemetryV3ImplementationPlan.md).
Its current implementation includes the stable
[reflection facade](reflection/Reflection.hpp),
[fixed wire type traits](type/Traits.hpp), and the
[canonical codec](codec/Codec.hpp) with caller-owned
[Workspace](codec/Workspace.hpp). The facade supplies aggregate and callable
facts plus a normalized enum dictionary. The traits classify supported types
and calculate wire size, nesting depth and expanded value nodes during
compilation. The model, endpoint bindings and complete wire protocol belong
to later stages; this codec is a low-level API, not a transport endpoint.

The payload has exact `wireSize<T>` bytes: scalar little-endian bytes, enum
underlying code, arrays in element order, and aggregates in member order.
There are no C++ padding bytes, tags, or per-value lengths. A decoder first
checks the exact length and every `bool` byte (only 0/1), then constructs a
fully initialized object in the caller's storage. Unknown enum codes remain
valid values of the underlying type. A malformed payload never yields an
object to the caller.

```cpp
struct Reading { float volts; std::uint16_t status; };
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

[ServiceResult<T>](result/ServiceResult.hpp) already supplies status and
optional payload lifetime for the Stage 04 return-ABI check. For a large
response, use `successFrom([] { return responseValue(); })`: its prvalue is
constructed in the result's destination storage. The convenience
`success(value)` is limited to 256-byte payload objects, because accepting a
4 KiB value by parameter produced an 8 KiB stack frame on the tested ARM
compiler. Service endpoint factories and dispatch will be added later.

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
