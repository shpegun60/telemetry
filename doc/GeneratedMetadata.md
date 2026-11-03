# Optional structural SDK metadata

Use structural metadata when an application knows its device schema at build
time but needs to enumerate that schema, for example to construct an inspector.
It adds data alongside the ordinary DTOs and client bindings:

```sh
telemetry-codegen generate descriptor.bin --targets cpp,js,ts \
  --namespace device_api --structural-metadata --output generated
```

The flag is opt-in. Ordinary generated SDK files keep the same contents.
`manifest.json` records the option and owns the extra files. Regeneration
without the option removes its formerly owned metadata files and preserves
unowned application files. `--check` verifies metadata too.

| Target | Additional files | Entry point |
| --- | --- | --- |
| Every selected target | `metadata/schema.json` | JSON structural document |
| C++ and QML | `cpp/<namespace>/Metadata.hpp` | `<namespace>::metadata` |
| JavaScript | `js/device.metadata.js` and `.d.ts` | `structuralMetadata` |
| TypeScript | `ts/device.metadata.ts` | `structuralMetadata` |

The C++ header uses only standard fixed records, `std::array` and
`std::string_view`. It requires no Descriptor parser, RuntimeSchema, transport,
Model, Qt, allocator or dynamic initialization. JS/TS metadata is a separate
deeply frozen module; importing ordinary `device.js` does not import it.
TypeScript uses a discriminated `StructuralType` union, so `kind === 3` exposes
`members` without a type assertion.

## Identity and graph

The document declares `format: "telemetry-structural-metadata"`, `version: 1`,
Descriptor `wireVersion`, fingerprint, SHA-256 and the exact 64-byte Descriptor
header. The metadata version is separate from the SDK package version and wire
versions. A consumer must reject an unsupported format/major version rather
than guessing its layout. Future additive fields can be ignored; required
fields and their meanings remain part of version 1.

`types` preserves every positional TypeId, including builtin and unreachable
types. Struct members reference TypeIds and include their original name,
language symbol and local `wireOffset`. Array element counts are retained even
when elements occupy zero bytes. Enum codes are canonical decimal strings in
JSON/JS/TS, including S64 minimum and U64 maximum; enum dictionaries remain
open and do not impose a closed set of accepted codes.

Catalog records retain category, group, category-local `firstInCategory`, count
and names, including empty catalogs. Endpoint records retain category, full
32-bit packed ID, catalog, value/request/response TypeIds and permissions.
The same numeric endpoint ID may occur in different categories. Original
names are string values, including names such as `__proto__`; generated
language symbols are reported separately.

An endpoint's `catalogIndex` is its category-local catalog group/index. Resolve
it using `(endpoint.category, catalog.group === endpoint.catalogIndex)`; it is
not an index into the flattened `catalogs` array containing all categories.

In C++, the arrays are `typeRecords`, `memberRecords`, `enumRecords`, `catalogs`
and `endpoints`. Type records contain the first/count ranges of member and enum
records. Interpret ranges according to kind and count; an unused first index
need not be zero. A zero-byte member can have an offset equal to its parent's
wire size. Enum `bits` stores the integer modulo 2^64: signed narrow `-1` is
`0xffffffffffffffff`, not a width-masked wire value. Derive signedness and wire
width through the enum's `relatedType` scalar. Enum entries retain numeric
Descriptor order; raw unsigned `bits` are not sorted for signed enums.

## Boundaries

Offsets describe canonical encoded data, never native C++ DTO layout. Do not
apply them to a DTO pointer or infer `offsetof`, alignment or object lifetime.
Metadata is descriptive; endpoint invocation still uses the existing typed or
dynamic client, admission, session and FIFO contracts.

Units, UI controls, ranges, defaults, translations and layout hints belong in
separate overlays. They cannot grant write permissions or replace wire
validation. Metadata alone does not create a GUI, open a session, download a
Descriptor or prove that a connected peer matches the generated schema.

[Qualification](../tests/sdk_metadata/README.md) checks independent Descriptor
fixtures, every record through actual C++/JS/TS consumers, immutability,
standalone headers, output ownership and selected Cortex-M7 roots.
[Installed SDK packages](SDKPackages.md) describe the host-tool distribution.
