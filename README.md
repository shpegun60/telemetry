# Telemetry

C++20 native typed telemetry for embedded applications, with a Qt playground.
The playground uses simulated values; it does not connect to an instrument.

Repository: [shpegun60/telemetry](https://github.com/shpegun60/telemetry).
Own code is [MIT licensed](LICENSE). Pinned [Boost.PFR](lib/boost_pfr/VERSION.md),
[magic_enum](lib/magic_enum/README.md) and [delegate](lib/delegate/README.md)
sources and their original licenses are included. No download, generator or
analyzer firmware checkout is required to use the library.

## Three endpoint families, one Model

- Field: a getter returns native `T` or `const T&`; an optional setter takes that exact
  `T` or `const T&` and returns `WriteResult`.
- Command: no request or one aggregate request, returning `CommandResult`.
- Service: no request or one aggregate request, returning an aggregate
  response by value or `const Response&`, `void`, `ServiceResult<Response>`
  or `BorrowedServiceResult<Response>`.

Callbacks are `noexcept`. Declarations contain a name and a binding.
Units, application limits, initial values and validation belong to the
application. One immutable type registry describes the structural types
used by all three families. There is no old Scalar container or v2 adapter.

```cpp
#include <telemetry/Telemetry.hpp>
namespace ts = telemetry;

struct Config { float voltage; bool enabled; };
struct Device {
    Config state{230.f, true};
    Config read() const noexcept { return state; }
    ts::WriteResult write(const Config& value) noexcept {
        state = value;
        return ts::WriteResult::Applied;
    }
};
inline Device device;
inline constexpr ts::FieldTable localFields{
    ts::field<&Device::read, &Device::write>("Config", device)
};
inline constexpr ts::FieldCatalogTable fields{
    ts::group("device", localFields)
};

// Local compile-time position:
// auto value = localFields.read<0>();
// localFields.write<0>(Config{240.f, true});
// Global compile-time packed ID:
// auto value = fields.read<ts::makeId<0, 0>()>();
// Runtime exact native type:
// auto value = fields.readAs<Config>(runtimeId);
```

See the complete mixed Field/Command/Service example and encoded API in
[the library README](lib/telemetry/README.md). Runtime packed IDs are u32,
with 16-bit catalog and entry positions: lookup is bounded array indexing.
Compile-time local/global calls retain the concrete target and native type.
Runtime `readAs`/`writeAs` provide checked numeric conversion or the exact
structural type. Owners, callable objects and borrowed names must outlive
their tables. Late binding uses the [slot family](lib/telemetry/slot/README.md).

## Files and consumers

```text
lib/telemetry/Telemetry.hpp       sole telemetry umbrella, namespace telemetry
  reflection/, type/             normalized reflection and TypeRegistry
  codec/, result/, slot/          canonical LE values, Workspace, statuses, binding
  field/, command/, service/     definitions, local tables and global catalogs
  model/, abi/, detail/          mixed Model and compiled encoded adapter
lib/resource/                    independent flat C++20 filesystem
  protocol/                      optional LIST/STAT/READ/WRITE framing
  telemetry/v3/                  optional DescriptorFile and ValuesFile
web/telemetry.js                 strict v3 descriptor/value codec for JS
examples/structured_client/      browser and Qt client examples
examples/structured_protocol/    optional transport-owned Bind/Exchange example
app/                            simulated Qt demo and resource facade
tests/                          maintained host, compiler, ARM and hardware checks
doc/                            implementation plan, migration and evidence
build/                          local generated artifacts, ignored by Git
```

[Resource](lib/resource/README.md) is independent of telemetry. Its optional
[v3 providers](lib/resource/telemetry/v3/README.md) expose
`/telemetry/descriptor.bin` and `/telemetry/values.bin`. The descriptor
fingerprint identifies their type/layout agreement. Transport connection
agreement and correlation belong to the application; the
[Bind/Exchange example](examples/structured_protocol/README.md) illustrates
one bounded implementation without adding fingerprint checks to every call.

Encoded object storage is chosen at compile time. The default local budget
is 32 bytes (`TELEMETRY_STRUCTURED_LOCAL_BYTES`); larger objects use
caller-owned Workspace. Service counts its request and actual result object
together. The budget changes storage only, never validation or wire bytes.
Const-reference outputs return small const views and encode directly from the
application object; they do not materialize that result in Workspace. The
application keeps it alive and stable. See the
[borrowed output contract](doc/BorrowedNativeValues.md).
An owning native return of a large object can use a large application stack;
the encoded Workspace path is available for those values.

## Build and verify

Open `telemetry.pro` with an installed Qt 6 MinGW kit. It includes
`lib/telemetry/telemetry.pri` and selects `resource_telemetry` for the demo.
`resource.pri` includes the generic filesystem/protocol sources and selects
the v3 provider only with `resource_telemetry`. Bind/Exchange is a separately
included example. Generic resource-only consumers need no PFR.

```sh
python tests/run_checks.py --build-dir build/checks
python tests/resources/run.py --build-dir build/resources
python tests/structured/qualification/run.py --build-dir build/qualification
```

The [test index](tests/README.md) describes the maintained matrix.
[H7S qualification](tests/structured/mcu/h7s/README.md) separates actual
cycles/observed call-chain stack from offline disassembly and compiler
frames. [Migration evidence](doc/StructuredTelemetryV3MigrationGuide.md)
records the transition to this single API; old review reports and
[historical measurements](doc/evidence/pre-unification/README.md) retain
their original source identity.
The [final qualification](doc/StructuredTelemetryV3FinalQualification.md)
collects final-source software and H7S results with their publication gate.

The final C++20 API intentionally has no source/ABI or wire compatibility
adapter for the retired Scalar/v2.1 implementation. Existing consumers
must follow the migration guide rather than mix the two descriptors.
