# Telemetry resource adapters (C++20)

Authors: Ruslan Kovtun (shpegun60), codexAi. [MIT](../LICENSE).

Three providers connect public telemetry metadata to resource streams:

```cpp
#include <resource/telemetry/TelemetryFiles.hpp>
#include <resource/FileSystem.hpp>

telemetry_resource::SchemaFile schema{fields.index()};
telemetry_resource::CommandsFile commandSchema{commands.index()};
telemetry_resource::ValuesFile values{fields.index()};

constinit const auto fs = resource::filesystem(
    resource::file("/telemetry/schema.json", schema),
    resource::file("/telemetry/commands.json", commandSchema),
    resource::file("/telemetry/values.json", values));
```

The provider constructors copy the index pointer/count, including from a
temporary `.index()`. Catalogs, descriptors and metadata must remain immutable
at stable addresses for the providers' lifetimes. Constructors count the exact
output size once; they never read values or execute commands. Finish startup
before exposing the providers to transports. The file table itself supports
constant initialization and read-only storage.

Use the single resource qmake include with the telemetry option:

```qmake
include(lib/telemetry/telemetry.pri)
CONFIG += resource_telemetry
include(lib/resource/resource.pri)
```

The adapter requires normal telemetry JSON support for the public fingerprint
functions. The resource include selects C++20 without changing telemetry's
independent language requirements. Compile the three provider `.cpp` files;
the core library and its existing serializers are unchanged. Provider headers carry telemetry's
exact ABI tag in their constructor symbols to reject incompatible compiled
layouts. Keep them at the assembly point; consumers include only
[DeviceResources.hpp](../../../app/resources/DeviceResources.hpp), which has no
telemetry includes. The new adapters do not include telemetry's private helpers.

## Schema and commands

SchemaFile emits the current field schema, including `meta.formatVersion`,
the reflected `fieldFlags` dictionary, per-field flags, bounds, defaults and
enum dictionaries. CommandsFile emits the grouped command schema through
`forEachParameter`; temporary parameter references are consumed synchronously.
The v1 resources use the default numeric representation for 64-bit JSON values.
The existing JSON API continues to offer its separate string option.

Counting and chunk output use the same emission path. Immutable text may be
split anywhere, including within an escaped name or UTF-8 sequence. Chunks
must be concatenated before interpreting the JSON. No full file or temporary
dictionary is constructed. Repeated valid cursors produce identical bytes
while metadata stays unchanged. Floating metadata uses a bounded decimal
formatter with 17 significant digits and nearest, ties-to-even rounding. It
uses integer arithmetic, no allocation, no locale state and no large lookup
tables. Differential tests compare it with both `to_chars` and `snprintf`,
including subnormals, rounding ties and the largest finite values. This
formatter is used for schema bounds/defaults, not live hex values.

The private cursor is a u32 logical record ordinal followed by a u32 byte
offset within that record. A record is a document/catalog delimiter or one
field/command description. Both fit because total file length is checked
against u32 and every record contributes bytes. Its packing is private, not a
wire interpretation clients should recreate. The core forwards it unchanged.
EOF has a continuation cursor too and is safe to read again, including with
an empty output span. Invalid record/offset values produce InvalidCursor.

Resuming skips whole catalogs and entry prefixes using their counts, without
serializing earlier fields. The current record is regenerated up to its byte
offset; a very long field dictionary or command with many parameters can
therefore require repeated work with tiny chunks. The adapter stores no per-
client state and imposes no artificial 16-bit limit on those byte offsets.
Malformed metadata or an output length exceeding u32 makes `size()==0` and
read return InvalidData. STAT still reports the provider's declared size;
the read status is authoritative for content validity.

## Live values

ValuesFile is a separate machine representation; existing `writeValues()`
retains its numeric JSON format. Example:

```json
{"meter":["003f800000","001234","0100000000"]}
```

Every string is two hex status digits followed by fixed-width payload hex:
00 = available, 01 = Null/unavailable. All unavailable payload digits are zero.
Payload widths are U8/S8/Bool=2 hex digits, U16/S16=4, U32/S32/F32=8,
U64/S64/F64=16; a Null descriptor has only status `"01"`. Bool uses 00/01.
Payload digits are most significant first, independently of CPU endianness.
Signed integers use their low two's-complement bits; float/double use
`std::bit_cast` of the normalized value returned by `Field::read()`.
IEEE binary32/binary64 is checked at compile time. Normal telemetry conversion
rules apply before representation, so a failed conversion becomes unavailable.

One whole value token, including its leading comma, is atomic. Before calling
a getter, the adapter checks the complete token width. If it does not fit,
that token's cursor remains unchanged and the getter is not called. Previously
emitted document text can still be returned as a successful partial chunk.
When nothing fits, the result is BufferTooSmall with zero bytes and the input
cursor. The largest atomic token is 21 bytes. Metadata text can use smaller
chunks, but a values client must eventually provide at least that much space.

`size()` depends only on immutable metadata, not readings. Each emitted value
calls its getter exactly once. Retrying the same cursor can produce a newer
whole value. There is no cross-field snapshot or synchronization; owners
provide these if needed. No getter runs during construction, size calculation,
invalid-token-offset handling or insufficient-token-capacity handling.
Tokens are written directly into their reserved output span. Hex formatting
uses 32-bit words; a 64-bit payload uses two words, and a float is never widened
to double for this representation.

See [resource contracts](../README.md), [protocol](../protocol/README.md)
and [validation](../../../tests/resources/README.md).
