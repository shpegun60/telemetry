# Binary telemetry resources (C++20)

Authors: Ruslan Kovtun (shpegun60), codexAi. [MIT](../LICENSE).

The adapter exposes three read-only files. They share the flat resource protocol
but have different payloads:

| Index | Path | Purpose |
|---|---|---|
| 0 | `/telemetry/schema.bin` | Descriptive fields, limits, flags and enums |
| 1 | `/telemetry/commands.bin` | Descriptive commands, parameters and enums |
| 2 | `/telemetry/values.bin` | Dense live values, decoded using the matching schema |

There is no decimal conversion, hex text, JSON escaping, allocation or complete
file buffer. The existing public telemetry JSON API is independent and unchanged.
This is a breaking resource format change: clients must use the binary decoder,
restart old cursors, and use the new paths. It is not a change to packet framing.

## Integration

```qmake
CONFIG += telemetry_no_json resource_telemetry
include(lib/telemetry/telemetry.pri)
include(lib/resource/resource.pri)
```

`telemetry_no_json` is optional; omit it if other application code uses the old
JSON API. There is one resource `.pri`, with no nested include cycle.

```cpp
#include <resource/FileSystem.hpp>
#include <resource/telemetry/TelemetryFiles.hpp>

telemetry_resource::SchemaFile schema{fields.index()};
telemetry_resource::CommandsFile commandFile{commands.index()};
telemetry_resource::ValuesFile values{schema};

constinit const auto files = resource::filesystem(
    resource::file("/telemetry/schema.bin", schema),
    resource::file("/telemetry/commands.bin", commandFile),
    resource::file("/telemetry/values.bin", values));
```

`ValuesFile{fields.index()}` is also supported. It computes the schema metadata
once to obtain its matching fingerprint. `ValuesFile{schema}` copies the cached
index, fingerprint, field count and value size instead; it does not borrow the
SchemaFile object's address.

Catalogs, descriptors, labels and metadata must remain immutable at stable
addresses for the providers' lifetimes. Owners may change values under the
application's synchronization policy. No cross-field snapshot is acquired.
Construction finishes before transfers start. Construction walks descriptions
once and invokes no getter or command. `size()` is a cached u32 load. Metadata
that cannot be encoded or exceeds the u32 file limit yields size zero and
`read()` reports `InvalidData`; valid empty catalogs still have a file header.
The ABI-tagged constructors retain telemetry's mixed-layout link protection.

## Common wire rules

Version 2.0 uses explicit little-endian integers, two's-complement signed bits,
and IEEE-754 binary32/binary64 float bits. No C++ struct memory is serialized.
Strings are `u32 byteLength` followed by exactly those bytes. Names from the
current C-string descriptor API end at the first NUL; enum names supplied as
`string_view` preserve embedded NUL. Quotes, newlines and backslashes need no
escaping. UTF-8 is a UI convention, not a wire constraint.

Version 2 widens schema and command fingerprints from u32 to u64. Metadata
headers grow from 40 to 44 bytes, and the values header from 16 to 20. Record
layouts and per-value tokens are unchanged. Update firmware and decoders
together and discard cached v1 metadata; the reference decoder rejects v1.
Rebuild all C++ resource consumers because cached provider layouts also change.

Stable type codes in [BinaryFormat.hpp](BinaryFormat.hpp) are independent of
telemetry's internal ScalarType ordinals:

| Type | Null | Bool | U8 | U16 | U32 | U64 | S8 | S16 | S32 | S64 | F32 | F64 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Code | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 |
| Payload bytes | 0 | 1 | 1 | 2 | 4 | 8 | 1 | 2 | 4 | 8 | 4 | 8 |

Metadata Scalars are `[u8 type][u8 state][u8 payloadSize][payload]`.
Canonical Null is `00 00 00`; other types use state 1 and their exact width.
Bool payloads are 0 or 1. Exact min, max and default are always sent, including
native endpoints. No implicit `null means native bound` convention is used.
The scalar codec preserves nonfinite IEEE patterns. FieldType's existing
validation of metadata is unchanged; live reads retain Field::read conversion
and availability semantics.

Every schema/command record has an eight-byte header:
`u8 type, u8 version=1, u16 flags=0, u32 payloadSize`.
Unknown records can be skipped by size. A reader must reject unsupported file
major versions. Record counts exclude the file header. In the tables below,
`string` means length-prefixed bytes, while paired labels have both lengths
before their bytes.

## schema.bin

The 44-byte header contains, in order:

```
char[4] magic = TSCH
u16 major, minor
u32 headerSize, totalSize
u64 schemaFingerprint
u32 recordCount
u32 catalogCount, fieldCount, enumEntryCount, flagDefinitionCount
```

| Record type | Payload in wire order |
|---|---|
| 1 FieldFlagDefinition | `u32 value, string name` |
| 2 Catalog | `u32 catalogIndex, fieldCount, string name` |
| 3 Field | `u32 catalogIndex, fieldIndex, fieldId, policyFlags, enumCount; u8 declaredType, valueType, accessFlags, fieldFlags; u32 nameLength, unitLength; name bytes, unit bytes; Scalar min, max, default` |
| 4 FieldEnumEntry | `u32 fieldId, ordinal; Scalar code; string name` |

Order is flag definitions, then each catalog followed by its fields; each field
is immediately followed by its enum records. Flag names are reflected from
FieldFlag with magic_enum's flags mode. `accessFlags` is Readable=1, Writable=2,
using descriptor capability, not temporary slot availability. `fieldFlags`
Reserved=1 identifies an empty positional descriptor (Null type, no callbacks).
Policy flags are separate. `fieldId = catalogIndex * 65536 + fieldIndex`.
`valueType` specifies the payload width in values.bin; a client must not infer
that width from some other member. Both type members currently have equal values.

## commands.bin

The 44-byte header contains:

```
char[4] magic = TCMD
u16 major, minor
u32 headerSize, totalSize
u64 commandsFingerprint
u32 recordCount
u32 catalogCount, commandCount, parameterCount, enumEntryCount
```

| Record type | Payload in wire order |
|---|---|
| 1 Catalog | `u32 catalogIndex, commandCount; string name` |
| 2 Command | `u32 catalogIndex, commandIndex, commandId, parameterCount, commandFlags=0; string name` |
| 3 Parameter | `u32 commandId, parameterIndex, parameterFlags=0, enumCount; u8 type, presenceFlags; u16 reserved=0; u32 nameLength, unitLength; name bytes, unit bytes; Scalar min, max, default` |
| 4 ParameterEnum | `u32 commandId, parameterIndex, ordinal; Scalar code; string name` |

Order is catalog, command, parameters; each parameter precedes its enum records.
Presence bits HasName=1 and HasUnit=2 distinguish a missing pointer from an
explicit empty string. An absent label has length zero. Descriptions are visited
synchronously; temporary CommandParam references are never retained. Reserved
commands have no parameters and flags zero. This file describes commands only;
it is not a new execution protocol.

## values.bin

The 20-byte header is `TVAL, u16 major, minor, u64 schemaFingerprint, u32 fieldCount`.
Then every field, including reserved entries, contributes exactly:

```
u8 status       // 0 available; 1 unavailable
payload         // width from the matching schema's valueType
```

Unavailable payloads are zero filled. A Null field contributes one unavailable
status byte. There are no IDs, types or lengths per value. Fields appear in
catalog/field position order, so size is exactly
`20 + sum(1 + payloadSize(field.valueType))` and does not change with readings.

The full token (at most 9 bytes) must fit before its getter is called. A getter
runs exactly once for each emitted token; measuring, skipping, invalid offsets
and insufficient space never call that getter. Retrying may return a newer
whole value. Different fields/chunks may reflect different instants.

## Cursors and bounded output

Cursors are opaque `u64` values: bits 63..62 select the block kind, bits 61..30
hold a 32-bit key, and bits 29..0 hold the byte offset inside that block. Cursor
zero starts the prefix. This replaces the ordinal cursor used through `f17a253`;
restart at zero after upgrading. The binary v2 payload and fingerprints do not change.

| Kind | Key | Schema | Commands | Values |
| --- | --- | --- | --- | --- |
| 0 Prefix | must be 0 | header + flag definitions | header | header |
| 1 Catalog | catalog position | catalog record | catalog record | invalid |
| 2 Entry | packed field/command ID | field + enum records | command + parameter/enum records | one atomic value |
| 3 End | must be 0, offset must be 0 | EOF | EOF | EOF |

EOF is exactly `3ull << 62`. Reading it returns Ok, zero bytes, EOF and the same
cursor. Prefix/catalog/metadata offsets may reach the exact block size, which
advances to the next block; larger offsets are invalid. Value offsets must be
zero. Each metadata block must fit `(1u << 30) - 1` bytes; provider construction
rejects larger blocks with size zero and InvalidData. Total file size remains u32.
Catalog keys are checked before narrowing to the 16-bit group type. Advancing
past field/command ID `0xffffffff` reaches EOF without wrapping.

A pause after byte or cursor progress returns Ok. If the next atomic value
cannot fit an otherwise empty output and no progress was made, BufferTooSmall
retains the cursor. Errors commit zero bytes and retain the original cursor;
callers must discard any output prefix. Use at least 9 payload bytes to guarantee
live value progress (plus the protocol's 12-byte READ envelope where applicable).
Output storage must not overlap immutable provider metadata.

Resume resolves the selected descriptor directly through the positional index.
No earlier catalog, field or command description is traversed. Local records
inside that selected field/command block may still require traversal and string
length calculations. The original sequential metadata callbacks serve this local
walk; counts are read directly from the new ops tables. Record lengths come from checked
arithmetic and string references prepared once for that record. READ never runs
an encoder just to count its bytes, and metadata records skipped by a local
offset do not run their encoders. Construction retains the original semantic
hash pass. Values scan only any
consecutive empty groups *after* the current position. No per-client state,
global seek tables or extra descriptor storage is allocated.

## Semantic fingerprints

Fingerprints belong to this binary adapter and do not call telemetry schemaCrc.
FNV-1a 64 starts at `0xcbf29ce484222325` and updates
`(hash XOR byte) * 0x100000001b3` modulo 2^64, as defined in
[RFC 9923](https://www.rfc-editor.org/rfc/rfc9923.html#section-2).
First hash four magic bytes (TSCH or TCMD), then LE u16 major and minor. Hash
records as `[u8 type, u8 1, u16 0, payload, u32 payloadSize]`, without padding.
Semantic record order is:

- Schema: flag definitions; each catalog; for each field, its enum entries
  before its Field record.
- Commands: each catalog; for each command, each parameter's enums before that
  Parameter record, followed by the Command record.

This postorder discovers child counts in one construction pass. Wire output
remains parent first. Headers, cached sizes and live values are not hash inputs;
all descriptive payloads, positions, labels, capabilities, exact scalar bits
and dictionaries are. Goldens pin the algorithm. Commands have a separate
fingerprint; values carry the schema fingerprint. The decoder compares all 64
bits and rejects a mismatch, so the client must reload the schema.

The wider hash reduces accidental matches but remains a change hint, not a
collision-free identity or payload integrity check. Two FNV32 streams with
different seeds are not used as a substitute for FNV-1a 64. Cached hashes use
two u32 words to avoid extra ARM32 alignment padding; only construction hashes
descriptions. Reads copy the cached value and never hash metadata again.

## Browser

[telemetryBinary.js](../../../web/telemetryBinary.js) exports `parseSchema`,
`parseCommands`, `parseValues`. Serve files as `application/octet-stream`:

```js
import {parseSchema, parseValues} from './telemetryBinary.js';
const schema = parseSchema(await fetch('/telemetry/schema.bin').then(r => r.arrayBuffer()));
const readings = parseValues(await fetch('/telemetry/values.bin').then(r => r.arrayBuffer()), schema);
console.log(schema.catalogs[0].fields[0].name, readings.values[0].value);
```

U64/S64 and fingerprints are BigInt, never rounded Number. Scalars retain raw
payload bytes (NaN payloads remain inspectable). Strings expose decoded text and a `nameBytes` or
`unitBytes` array; invalid UTF-8 gives null text with exact bytes retained.
Parameter presenceFlags still distinguish absent text. Unknown record types or
versions are retained in unknownRecords. Missing required definitions are
rejected, because an incomplete schema cannot safely decode dense values.
Bounds, counts, positional identities, bool/status bytes and fingerprints are
validated. The parser accepts ArrayBuffer and views with nonzero byteOffset.
For debug JSON on the PC use a BigInt/Map replacer:

```js
JSON.stringify(schema, (_, value) => typeof value === 'bigint' ? value.toString()
    : value instanceof Map ? Object.fromEntries(value) : value, 2);
```

See [tests and measured footprint](../../../tests/resources/README.md).
