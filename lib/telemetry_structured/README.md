# Structured telemetry (under construction)

Authors: Ruslan Kovtun (shpegun60), codexAi. Own code: MIT.

The new C++20 module follows the
[structured v3 implementation plan](../../doc/StructuredTelemetryV3ImplementationPlan.md).
Its current implementation includes the stable
[reflection facade](reflection/Reflection.hpp) and the
[fixed wire type traits](type/Traits.hpp). The facade supplies aggregate and
callable facts plus a normalized enum dictionary. The traits classify supported
types and calculate wire size, nesting depth and expanded value nodes during
compilation. The codec, model, service bindings and wire format belong to later
stages and are not exposed as working APIs yet.

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
