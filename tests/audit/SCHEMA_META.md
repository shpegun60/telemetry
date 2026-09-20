# Minimal schema metadata

The JSON schema now has an explicit format version. Field schemas include one
dictionary of known policy bits:

```json
{
  "schema": "<fingerprint>",
  "meta": {
    "formatVersion": 1,
    "fieldFlags": {
      "type": "u32",
      "values": {"1": "Persistent"}
    }
  },
  "catalogs": []
}
```

Each field still carries its numeric `f` mask. Dictionary keys represent masks,
not bit positions; zero means no flags. Unknown bits remain intact in `f` and
must be ignored by a consumer which does not recognize them. The dictionary is
generated at compilation using
`magic_enum::enum_entries<FieldFlag, magic_enum::as_flags<>>()`. There is no
manual duplicate list, and no runtime enum scan. Flags mode scans individual
bits across the full U32 underlying type; zero/composite aliases are omitted.
Names are written through their string_view length, without assuming NUL termination.

Both local and grouped command schemas contain only
`"meta":{"formatVersion":1}`. They do not carry unused field-flag metadata.
Values JSON has no new metadata. Build details, ID-layout information, device
state and additional profiles belong in separate documents if needed later.

`jsonSchemaFormatVersion` is independent of `telemetryAbiVersion` (still 7)
and any future persistent-storage version. A schema without `meta` is an older
envelope. The format version enters all schema fingerprints; reflected flag
codes/names additionally enter field fingerprints. Therefore caches refresh
when the envelope or flag dictionary changes. Adding a named bit does not
itself change the JSON shape and need not bump the format version.
The immutable header's fingerprint prefix is also evaluated as constexpr;
runtime schema hashing starts from that constant instead of rehashing its names.

The field serializer's compilation unit now includes the pinned magic_enum
header. Numeric-only core headers and command-only JSON remain independent of
reflection. Read/write/call dispatch, Field/Command layouts and value encoding
are unaffected.

## Verification

- `TelemetryJsonCheck.cpp` checks the exact minimal headers, no duplicate root
  metadata, empty schemas, preservation of unknown high policy bits, both integer
  output modes and the absence of metadata in values.
- Compile-time reflection checks include `1u << 20` and `1u << 31`, as well as
  omitted zero/composite enumerators. These run with every host/ARM compilation
  of that suite.
- Existing field and command checks sweep all buffer capacities, including the
  new header, and retain early-stop and getter-side-effect checks.
- The empty field schema's fixed fingerprint is `110cc495`. The golden value
  pins version/dictionary ordering in addition to descriptor-dependent checks.
- The differential fixture compares with pre-metadata `a2339b2` (the serializer
  refactor `492805c` was already proven byte-identical to it). Only `meta` and
  the two fingerprints change; values and field/command rows must be identical.

```sh
python tests/audit/compare_flags_schema.py --baseline /path/to/a2339b2 --cxx g++ --output /path/to/meta-comparison --schema-meta
```

The compare script parses actual JSON from independently compiled executables;
it rejects any other descriptor/value change. The full host and ARM runners
exercise the new checks together with existing conversion, slot, ABI and codegen
guards. This is a schema addition, not a Flash persistence implementation.
