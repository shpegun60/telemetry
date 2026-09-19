# Telemetry library

Standalone catalog, numeric conversion and JSON library for C++17.
Authors: Ruslan Kovtun (shpegun60), codexAi.

Copy this directory and its sibling `delegate` and `magic_enum` directories
into a consumer, keeping them under the same `lib` directory, and include the reusable `.pri`:

```qmake
include(path/to/telemetry/telemetry.pri)
```

The library uses C++17 and bundled [tiny_delegate v1.1.0](../delegate/README.md).
It has no Qt, STM32, RTOS, Mongoose or `basic_types.h` dependency. For a
non-qmake build, add `lib/telemetry` and `lib/delegate` to the include paths
and compile `TelemetryJson.cpp`.
Optional `TelemetryEnum.h` uses bundled [magic_enum v0.9.8](../magic_enum/README.md).
Numeric-only headers and `TelemetryJson.cpp` do not include magic_enum.

Licensed under the [MIT License](LICENSE). Keep this license with copied or
redistributed library files; delegate and magic_enum retain their upstream MIT licenses.

## Public files

- `TelemetryScalar.h`: private variant storage for Null, Bool, F32/F64,
  U8/U16/U32/U64 and S8/S16/S32/S64, with checked accessors.
- `TelemetryCompiler.h`: shared `TELEMETRY_FORCE_INLINE` portability macro.
- `TelemetryGetter.h`: constexpr non-owning getters returning Scalar or native numbers.
- `TelemetrySetter.h`: optional write callbacks and `WriteResult`.
- `TelemetryConversion.h`: constexpr checked numeric conversions.
- `TelemetryFieldType.h`: numeric type, write limits/defaults and an optional schema description callback.
- `TelemetryEnum.h`: opt-in `enumType<E>()` factory with compile-time enumerator names.
- `TelemetryCatalog.h`: packed IDs, Field, Catalog and name uniqueness.
- `TelemetryIndex.h`: direct lookup, typed reads and optional static catalog binding.
- `TelemetryJson.h`: schema fingerprint, schema JSON and values JSON.

## Scalar types and defaults

`Scalar::fromU8/fromU16/fromU32/fromU64` accept the corresponding
`std::uintN_t`; `fromS8/fromS16/fromS32/fromS64` accept `std::intN_t`.
`fromF32`, `fromF64` and `fromBool` complete the value factories. All factories
are `constexpr` and `noexcept`. Existing tag numbers retain their values.
Ordinary numbers also convert implicitly: `Scalar value = 12.5f;`.
`Scalar::from(value)` deduces the source type. Supported inputs are bool,
integers up to 64 bits, float and double; pointers, strings and long double
require an explicit application conversion.

| Type | Exact accessor | JSON schema type |
| --- | --- | --- |
| U8 / U16 | `get<std::uint8_t>()` / `get<std::uint16_t>()` | `u8` / `u16` |
| U32 / U64 | `get<std::uint32_t>()` / `get<std::uint64_t>()` | `u32` / `u64` |
| S8 / S16 | `get<std::int8_t>()` / `get<std::int16_t>()` | `s8` / `s16` |
| S32 / S64 | `get<std::int32_t>()` / `get<std::int64_t>()` | `s32` / `s64` |
| F32 / F64 | `get<float>()` / `get<double>()` | `f32` / `f64` |
| Bool | `get<bool>()` | `bool` |

`Scalar` owns a private `std::variant` of `std::monostate` and these eleven
native types. `type()` derives the tag from the active alternative; no public
tag or payload can be changed independently. All alternatives have trivial,
nothrow copy/move operations, so the supported API cannot produce a valueless
variant. No alternative allocates. Integer width and signedness remain part
of the Scalar type. Field reads and writes both normalize to `declaredType`:
for example, a U8 getter for a field declared U16 publishes a U16 value.

`get<T>()` copies the exact alternative, without numeric conversion. A wrong
type throws `std::bad_variant_access` when exceptions are enabled; in the
verified GCC configuration with exceptions disabled it terminates. For
non-throwing inspection use `getIf<T>()`, which returns a const pointer or
null. That pointer is borrowed until the Scalar is reassigned or destroyed;
borrowing from a temporary is rejected at compilation. `get<T>()` returns a
value and is safe on a temporary. Example:

```cpp
Scalar value = 12.5f;
if (const float* number = value.getIf<float>()) {
    // Use *number while value stays alive and unchanged.
}
value = Scalar::fromU64(UINT64_MAX); // Tag and value change together.
auto count = value.get<std::uint64_t>();
```

The old public `type` member becomes `type()`; old union members become the
accessors above. The in-memory layout changes, even though the measured ARM
size stays 16 bytes: rebuild consumers and never use the object bytes as a
wire or persistent format. Factories, tag numbers and JSON formats remain
compatible. `Scalar::NativeType<ScalarType::F32>` is `float`; this mapping is
derived directly from the variant alternatives and also drives inferred reads.

Plain default declarations work without braces:

```cpp
Scalar value;    // Null.
Field field;     // id 0, name/unit "", declaredType Null, get/set nullptr.
Catalog group;   // id 0, name "", fields nullptr, count 0.
```

The same defaults apply in `constexpr` declarations and when trailing
members are omitted from a Field initializer. Field remains an aggregate.
An empty getter returns Null. Assign real names/IDs and types before using
default fields as published catalog entries.

## Write limits and defaults

`FieldType` always supplies `minimum()`, `maximum()` and `defaultValue()` as
Scalar values of the declared numeric type. `ScalarType::F32` in a row still
works: its limits are `numeric_limits<float>::lowest()` and `max()`, and its
default is zero. All integer types use their native extrema; Bool uses
false/true with default false. Null and unknown types have Null metadata.
Floating bounds use `lowest()`, not `min()` (which is a small positive number).

Use a constexpr factory for custom definitions:

```cpp
constexpr auto voltage = numericType<float>(0.0f, 300.0f, 230.0f);
constexpr auto count = numericType<std::uint16_t>(10, 20, 15);
constexpr auto ordinary = numericType<double>(); // Native extrema, default 0.
constexpr auto anotherDefault = count.withDefault(12);
constexpr auto anotherRange = count.withLimits(0, 100, 50);
```

The three arguments undergo checked conversion to the declared type, using
the same rounding/truncation policy as writes. All must be finite and satisfy
`minimum <= default <= maximum`. Invalid constexpr definitions fail compilation
at `invalidFieldLimits`; the same programming error terminates via `std::abort`
when constructed at runtime. No exceptions are required. Runtime user input
belongs in `write()`, which reports `InvalidValue` normally.

Write order is: find the field, check setter presence, convert to declaredType,
check the inclusive numeric interval, then call the setter once. No clamping
occurs. U16 with limits 10..20 accepts 20.9 as 20 and rejects 9.9 as 9. Full
native integer intervals skip redundant comparisons. Floating writes reject
NaN and infinity even with native default bounds.

Reading only normalizes to declaredType. It neither checks nor clamps to
min/max, including in typed/inferred reads and `writeValues()`. A getter
returning 100.75 for a U16 field with limits 10..20 publishes 100. Raw numeric
conversion and floating reads retain their previous NaN/Inf behavior; value
JSON still represents non-finite readings as null.

Default is descriptive metadata, not an automatic write or fallback reading.
To apply it explicitly, call `field.write(field.declaredType.defaultValue())`.
Every schema field, including read-only fields, exports `min`, `max` and
`default`; Null metadata exports null properties. Changes to any of these
values change the schema fingerprint.
F32 metadata uses 17 significant digits for its promoted double value, so
clients parsing JSON numbers as double can write advertised extrema back
without crossing the native F32 range. The Qt schema display keeps the raw
JSON text, preserving U64/S64 metadata digits too.

Internally one private variant stores a triple of native numbers under one
tag. This avoids three separate Scalar tags and keeps all three values of
one type. On ARM32 FieldType occupies 40 bytes and Field 80, rather than the
96-byte Field needed by three separate Scalars. There is no heap allocation
or borrowed pointer to temporary bounds. Constant tables can remain in Flash;
runtime tables occupy their owner's storage. Rebuild consumers for this layout.

## Enum dictionaries for schemas

An enum describes names for a numeric field. Its underlying type determines
the existing Scalar type; there is no `ScalarType::Enum` or enum alternative
in the variant. Include `TelemetryEnum.h` where enum fields are defined:

```cpp
enum class Mode : std::uint16_t { Off, Auto, Manual };
Mode mode = Mode::Auto; // Owner's storage, with a stable lifetime.
using RawMode = std::underlying_type_t<Mode>;

constexpr Field fields[] = {
    {makeId(0, 0), "Mode", "", enumType<Mode>(),
     []() noexcept { return static_cast<RawMode>(mode); },
     [](const Scalar& value) noexcept {
         mode = static_cast<Mode>(value.get<RawMode>());
         return WriteResult::Applied;
     }},
};
constexpr Catalog catalogs[] = {{0, "settings", fields}};
constexpr auto index = CatalogIndex::bind<catalogs>();

auto number = index.read<makeId(0, 0)>(); // optional<uint16_t>, not optional<Mode>.
auto result = index.write(makeId(0, 0), 100); // InvalidValue: outside 0..2.
```

The owner explicitly casts to/from its enum. Getters, setters and public
read/write calls continue to use native numbers or Scalar. During compilation,
`enumType` derives min/max from its listed codes and chooses the smallest
code as default. `enumType<Mode>(Mode::Auto)` overrides that default while
retaining automatic bounds. `.withDefault(number)` and `.withLimits(min, max,
default)` also work and retain the dictionary.

Writes check only the resulting numeric interval. Gaps between named codes
remain writable: codes 0 and 10 admit 5. Membership/semantic validation belongs
to the owner. Reads ignore the write interval and may publish code 100.

`writeSchema()` emits the numeric type and an extra property:

```json
{"t":"u16","min":0,"max":2,"default":0,"enum":{"0":"Off","1":"Auto","2":"Manual"}}
```

`writeValues()` still emits numbers. Lookup and read/write never inspect enum
presence or invoke the description callback. Only schema serialization and
its fingerprint consume the dictionary. The factory retains a pointer to a
function specialized for the enum, which streams constant names and codes
on request. There is no owned runtime dictionary, dynamic registration or
name search. Names and description code still occupy program storage; final
Flash/RAM placement depends on the linker.

Automatic discovery uses magic_enum's configured range (default **-128..127**).
Values outside it are omitted from both names and automatic bounds, even
when other enumerators were found. For
sparse or large codes, list the values as template arguments; no scanning or
manually written label strings is needed, and the specified order is kept:

```cpp
enum class Code : std::uint64_t { Ready = 100000, Last = UINT64_MAX };
constexpr auto type = enumType<Code, Code::Ready, Code::Last>();
```

Alternatively, define `magic_enum::customize::enum_range<E>` in the shared
enum header before use. For example, `min = 0` and `max = 255` cover all U8
codes. All translation units must see identical enum definitions and reflection
configuration. Duplicate explicit codes, unnamed explicit values and an
empty automatic dictionary fail compilation. Aliases share one numeric key
and a compiler-selected name; aliases cannot be separate dictionary entries.
Only complete enum definitions expose names. For enums nested in class
templates, Clang may defer the enumerator list until a named enumerator is
used. Reference a value before automatic discovery (for example in a
`static_assert`), or use the explicit value pack, which also instantiates it.
Custom names use magic_enum's
customization API; provide valid UTF-8. JSON escapes quotes, backslashes and
control bytes, including embedded NUL. Full 64-bit dictionary keys are exact
decimal strings, independent of JavaScript Number precision.

`Field::declaredType` is now a `FieldType`, implicitly constructible from and
convertible to `ScalarType`. Existing aggregate rows containing `ScalarType::F32`
and comparisons with ScalarType keep working. Use `ScalarType type = field.declaredType`
when a concrete enum is needed; `auto` now deduces FieldType. Assigning a
ScalarType before publication clears dictionary metadata and restores native
limits with zero/false default. Data paths never read the schema callback.

## Packed IDs and direct lookup

`FieldId` is a 32-bit unsigned value: high 16 bits are the zero-based group
number; low 16 bits are the zero-based position within that group.
`GroupId` and `FieldOffset` are 16-bit unsigned values. Component indices
range from 0 through 65535, allowing 65536 groups and 65536 fields per group.
Counts use `std::size_t`, so a full component capacity does not wrap to zero.
`idComponentCapacity` is 65536. Both ID zero and `UINT32_MAX` are usable.

```cpp
using namespace telemetry;

// meter and sensor are existing sources with appropriate lifetimes.
constexpr Field meterFields[] = {
    {makeId(0, 0), "Ua", "V", ScalarType::F32,
     []() noexcept { return Scalar::fromF32(meter.voltage); }},
    {makeId(0, 1), "Ia", "A", ScalarType::F32,
     +[]() noexcept { return Scalar::fromF32(meter.current); }},
};
constexpr Field sensorFields[] = {
    {makeId(1, 0), "Temperature", "degC", ScalarType::F64,
     Getter::bind<&Sensor::temperature>(sensor)},
};
constexpr Catalog catalogs[] = {
    {0, "meter", meterFields},
    {1, "sensor", sensorFields},
};
constexpr CatalogIndex index{catalogs};

const FieldId id = makeId(1, 0); // 65536: sensor temperature.
const Field* field = index.find(id);
Scalar value = index.read(id);
```

For this static example, source addresses must be usable in constant
expressions. The sources themselves may be mutable. An instance-bound table
uses the same constructors without `constexpr`; the owning object must keep
its address. The demo shows both forms.

The lookup splits the ID with `id >> 16` and `id & 0xffff`, checks the group
against the accepted group count, then checks the position against that
catalog's accepted field count. It computes the address directly. There is
no scan, binary search, hash, allocation, or per-field pointer table. Only
actually present groups and fields occupy storage; the full 16-bit capacity
is never reserved automatically. The lookup body is forced inline on
GCC/Clang/MSVC, including size-optimized builds on the verified ARM compiler.

`groupOf(id)` and `indexOf(id)` expose the two components. `CatalogIndex`
provides `find(id)`, `read(id)`, `read<T>(id)`, `write(id, value)`,
`catalog(group)`, `data()` and `size()`. `catalog()`
returns null for a group outside the accepted prefix. The view stores only
a catalog pointer and a group count and can be copied without copying rows.
For repeated reads, retain a found field pointer while its owner stays alive;
`field->read()` then has no ID lookup cost.

## Scalar, typed and inferred reads

```cpp
Scalar value = field->read();                    // Normalized to declaredType.
std::optional<float> voltage = field->read<float>();
Scalar byId = index.read(makeId(0, 0));
auto asDouble = index.read<double>(makeId(0, 0)); // optional<double>.
```

Each read invokes the selected getter once and normalizes its result to
`declaredType` using the same checked conversion as writes. Scalar reads
return Null for a missing field, empty/unavailable getter, Null/unknown
declared type or failed conversion. Typed reads then adapt that normalized
value to the requested C++ type. They return an empty `std::optional<T>` for
an unavailable value, missing ID or failure of either conversion. Valid zero
and false remain engaged values. Supported
destination types are unqualified native numeric values, without references.
Use `if (voltage)` before dereferencing, or `voltage.value_or(fallback)`.

```cpp
constexpr Field count{makeId(0, 0), "Count", "", ScalarType::U16,
                     []() noexcept { return 12.75; }};
auto scalar = count.read();        // U16 containing 12.
auto number = count.read<double>(); // optional<double> containing 12.0.
```

The declared type governs truncation, rounding and range even if the caller
requests a wider type. A getter returning 70000 cannot be read through a U16
field as double; that read is unavailable. A double or U64 getter read through
an F32 field is rounded to F32 before any requested widening. When the
requested C++ type is already the exact declared alternative, the typed path
performs the checked conversion directly, without an intermediate normalized
Scalar. Matching types do not undergo a numeric conversion.

When the metadata has static storage and is constexpr, bind the array into
the index's C++ type to enable an inferred destination:

```cpp
// catalogs is the namespace-scope constexpr array defined above.
constexpr auto fixed = CatalogIndex::bind<catalogs>();
auto ua = fixed.read<makeId(0, 0)>();          // optional<float>, F32 metadata.
auto temperature = fixed.read<makeId(1, 0)>(); // optional<double>, F64 metadata.
auto converted = fixed.read<float>(makeId(1, 0));
Scalar value = fixed.read(makeId(1, 0));
```

The returned type is `StaticCatalogIndex<catalogs>`. The compiler determines
the field and native result type; its value is still read at runtime. The
same type retains `find`, runtime-ID reads, writes and serialization support.
It has no mutable binding and implicitly supplies the shared const
`CatalogIndex` view to existing consumers. `bind` rejects non-constexpr
metadata immediately. `read<Id>()` rejects IDs outside the accepted prefix,
Null metadata and unknown declared types at compilation. Empty or unavailable
getters still need an optional result even for a valid compile-time ID.

An ordinary `CatalogIndex{catalogs}` stores only a pointer and count; its
table is not part of its C++ type. Use `read(id)` or `read<T>(id)` on that view.
Instance-bound catalogs such as DemoCatalog use this form.

## Contiguous-prefix contract

Both group numbers and local field positions must be dense and zero-based.
Validation occurs once when the corresponding object is constructed; a
`constexpr` construction performs it during compilation.

- `Catalog` accepts rows only while `row.id == makeId(group, rowPosition)`.
  Its public `count` is the accepted prefix length, not the requested length.
- `CatalogIndex` accepts catalogs only while `catalog.id == groupPosition`.
  Its `size()` is the accepted group prefix length.
- A gap, duplicate, reordered ID or wrong group component stops that prefix
  at the first mismatch. Later rows/groups are never skipped or renumbered.
- A field mismatch trims only that group. Correctly numbered later groups
  remain accessible. A group mismatch trims the entire group list.
- Counts above 65536 are capped before reading or narrowing an ordinal.
  Null input pointers produce an empty prefix, including with nonzero count.
- An empty group is a valid group position. A missing ID returns null; lookup
  never substitutes the last accepted field for an invalid request.

For example, field indices `0, 1, 4, 3` expose only `0, 1`; group IDs
`0, 1, 3, 2` expose only groups `0, 1`. Completeness can be checked explicitly:

```cpp
static_assert(catalogs[0].count == std::size(meterFields));
static_assert(index.size() == std::size(catalogs));
```

Those checks are optional: the library keeps the usable prefix instead of
asserting or rejecting all earlier valid entries. Do not renumber an existing
published field to conceal a gap; preserve its position with an unavailable
getter or deliberately change the schema/ID assignment.

Catalog metadata (`id`, `name`, `fields`, `count`) is immutable after
construction, so its validated limits cannot be overwritten. Catalogs can
be copy/move constructed but cannot be assigned. Construct a replacement
catalog when changing definitions. Default construction gives an empty
catalog with group ID zero and an empty name; normal published catalogs
still need unique names.

Array-reference constructors without a count deduce the actual extent.
Temporary arrays are rejected, including in explicit-count calls. Every
explicit count must describe a live array of at least that extent; the 65536
capacity cap cannot discover the allocation behind a pointer. Source field definitions,
array order and addresses must remain unchanged from Catalog construction
through the last read. Catalog arrays must outlive their CatalogIndex;
getters'/setters' source objects and all strings must outlive their consumers.
Changing the values inside source objects is supported. No locks or
cross-field snapshot guarantee are added.

## Getter forms

An ordinary noexcept function returning Scalar, bool, float, double or a
supported integer can appear by name or address. Both bare `[]` and `+[]`
captureless noexcept lambdas work in field rows;
`Getter::bind<&read>()` and `Getter::bind<&Sensor::method>(sensor)` are also
supported. Capturing lambdas need an explicit state owner and method binding.
Const objects work with const methods. Empty getters, including a typed null
function pointer, return Null. Sources need no telemetry return type:

```cpp
double Sensor::temperature() const noexcept { return temperature_; }
bool Sensor::enabled() const noexcept { return enabled_; }
float readUa() noexcept { return meter.voltage; }

constexpr Field fields[] = {
    {makeId(0, 0), "Ua", "V", ScalarType::F32,
     []() noexcept { return meter.voltage; }},
    {makeId(0, 1), "Ia", "A", ScalarType::F32,
     +[]() noexcept { return meter.current; }},
    {makeId(0, 2), "UaFunction", "V", ScalarType::F32, readUa},
    {makeId(0, 3), "Temperature", "degC", ScalarType::F64,
     Getter::bind<&Sensor::temperature>(sensor)},
};
```

Explicit `Scalar::fromF32(...)` and typed `-> Scalar` returns remain valid.
Getter initially packages its result using the native C++ type; `Field::read`
normalizes it to the field's declared type. Any supported numeric/bool source
type can therefore back any numeric/bool field, subject to the conversion
policy and value range. JSON and the Qt display use the same normalized read.
Calling `field.get()` directly is a low-level callback invocation that retains
the source type; use `read()` or `read<T>()` to apply the field contract.

Getter stores a trivial variant containing a `tiny::delegate_ref<Scalar()>`
or a native function pointer of its actual type. This keeps all forms
constexpr in C++17 without casting between incompatible function pointers.
Its invocation table is constexpr; dispatch is forced inline so known rows
can remove alternative selection even at `-Os`. Dynamic rows retain dispatch.
Conversion objects may throw during construction,
before an existing getter is replaced; invocation remains noexcept. Copying
a getter copies its binding, not its source. Use one tiny_delegate revision
and configuration throughout a program. GCC 13 UBSan has an upstream
constexpr function-template pointer comparison limitation; Clang can check
those expressions with sanitizers.

## Optional writes

Field's last member is `Setter set = nullptr`. Existing five-member rows
remain read-only. Setter is a noexcept policy wrapper over
`tiny::delegate_ref<WriteResult(const Scalar&)>`; an empty setter returns
ReadOnly, including when initialized or assigned a typed null pointer.
It accepts named functions, `&function`, bare/+ captureless lambdas,
`Setter::bind<&function>()` and `Setter::bind<&Owner::method>(owner)`.

Both public write methods are templates; ordinary callers use native values:

```cpp
auto result = index.write(makeId(0, 8), 250);   // int -> F32 VoltageLimit.
result = index.write(makeId(0, 8), 275.5);      // double -> F32.
result = field.write(12.7);                    // e.g. U16 receives 12.
```

The index uses the same direct lookup and accepted prefixes as reads.
It returns NotFound for a missing ID, ReadOnly for an empty setter,
InvalidValue when conversion or the numeric interval check fails, or the owner's result after one call.
The owner receives a Scalar already converted to `declaredType`. No getter
is invoked by a write. Explicit Scalar inputs are also accepted.

- Integer conversions preserve the value or reject an out-of-range result;
  they never wrap bits and never pass a 64-bit integer through double.
- Float-to-integer conversion discards the fraction toward zero, then checks
  the representable range before casting. Thus `-0.75 -> U8` gives zero,
  while `-1 -> U8` fails. The 2^63 and 2^64 upper endpoints are excluded.
- Finite zero becomes false; other finite numbers become true. Bool becomes
  numeric zero or one. NaN/Inf cannot be written into integer or bool fields.
- Floating targets may round. Finite overflow is rejected; underflow may
  round to zero. Finite min/max bounds reject NaN/Inf before the setter.
- Null and unknown destination tags are rejected. Compile without fast-math/finite-only
  assumptions so floating-point range checks retain their meaning.

Conversion is constexpr and shared by all field reads and writes. Equal native
types copy directly, without numeric conversion or representability checks. A Scalar
already carrying the requested numeric tag is also copied directly. Integer
widening omits bounds checks where every source value fits. Float inputs stay
float for bool/integer checks; there is no universal double or int64
intermediate. Float-to-double needs just widening; double-to-float checks
finite overflow and handles NaN/Inf explicitly. Necessary range checks remain
when a runtime value might not fit.
The separate write interval check runs after conversion; reads never apply it.

Float-to-integer uses a normal C++ `static_cast` after checking the bounds;
there is no separate round/trunc call in the implementation. Both Scalar reads
and writes normalize the actual value in place. Writes do not initialize an
empty Scalar before storing their converted value. Native input types and
constexpr metadata keep conversion choices visible to the compiler; runtime
metadata or a runtime Scalar tag still require dispatch.

Constant input/type pairs can be checked with static_assert. The typed
`convertScalar<T>(input)` returns `optional<T>`. Failed
`convertScalar(input, type, output)` leaves output unchanged, and input and
output may refer to the same Scalar.

WriteResult contains Applied, NotFound, ReadOnly, InvalidValue and Busy.
TypeMismatch is unnecessary because writes convert supported numeric types.
The owner checks its semantic range and provides synchronization. Applied
means the value was applied before returning; it does not imply persistence
to Flash. Queued writes need a separate completion contract.

The callback's argument is borrowed for the duration of the call only. Copy
it if it must be retained. Calling a populated `field.set(...)` directly is
a low-level operation: use `field.write(...)` or `index.write(...)` to obtain
type conversion and numeric interval checks. Bindings and metadata remain fixed during use.

## Serialization

Use the accepted view to avoid repeating even group-prefix validation:

```cpp
writeSchema(index, schemaBuffer, sizeof(schemaBuffer));
writeValues(index, valuesBuffer, sizeof(valuesBuffer));
auto fingerprint = schemaCrc(index);
```

The retained pointer/count overloads construct a CatalogIndex for that call.
All overloads publish exactly the accepted group and field prefixes, matching
lookup. The schema includes each group's numeric `id` and each field's local
`i` plus packed `id`:

```json
{"id":1,"name":"sensor","fields":[{"i":0,"id":65536,"n":"Temperature","u":"degC","t":"f64","w":false,"min":-1.7976931348623157e+308,"max":1.7976931348623157e+308,"default":0}]}
```

Values retain named arrays such as `{"sensor":[24.5]}`. The order-sensitive
FNV-1a fingerprint includes group IDs, all four field-ID bytes, declared
metadata, setter presence (`w`), min/max/default, enum codes/names/order when
present, and record/string boundaries. Limits are hashed by numeric value
bits with explicit byte order, never by object padding. This version adds
required metadata and changes fingerprints for numeric-only schemas too.
It is a version hint, not a promise
against collisions. The packed numbering and group schema IDs change the
previous playground schema; the production firmware is not changed.

Buffers belong to the caller; a zero returned length means failure and
partial JSON must not be sent. A null buffer fails regardless of its size;
a non-null buffer with positive size remains NUL-terminated. Serialization
stops at the first output failure, including further getter calls. Previously
read fields are not rolled back. The output must not overlap the metadata,
its strings or the source values. Names must be unique ASCII identifiers
(catalog names globally, field names within each catalog). Unit strings must
not contain JSON quotes, backslashes or control characters; strings must be
non-null. `names_unique` remains available for static field tables.

Null, failed normalization and non-finite floating-point values serialize as
JSON null. F32 and F64 use 9 and 17 significant digits respectively, preserving
round trips instead of shortening F32 to seven digits. JSON always uses a
decimal point, including under a decimal-comma locale; serialization does not
change the locale. The application must not concurrently call `setlocale`.
Floating formatting uses a bounded 64-byte temporary and the C library's
`snprintf`; newlib-nano builds need floating formatting enabled at link time
(for the verified CubeIDE configuration, `-Wl,-u,_printf_float`).

Every integer retains all decimal digits, including
`UINT64_MAX` and `INT64_MIN`; 8-bit integers serialize as numbers, not
characters. U64/S64 use bounded decimal conversion with unsigned magnitude
arithmetic, so INT64_MIN does not overflow and newlib-nano's optional
`long long` printf support is not required. JavaScript Number cannot represent every U64/S64 integer outside
`[-(2^53 - 1), 2^53 - 1]`. Serialization does not provide a write transport,
subscriptions or scheduling.

## Verification and Cortex-M7 code generation

[TelemetryCheck.cpp](../../tests/TelemetryCheck.cpp) exercises the public
contracts, including prefix clipping, endpoints and actual 65536-component
capacity. [TelemetryWriteCheck.cpp](../../tests/TelemetryWriteCheck.cpp) checks
numeric boundaries, all 121 numeric/bool conversion pairs, native getter forms,
setter policy and write dispatch. [TelemetryReadCheck.cpp](../../tests/TelemetryReadCheck.cpp)
covers all inferred types, explicit reads, optional/Null handling, declared
type normalization, single getter invocation, variant access and floating
endpoints. It checks all 121 source/declared type pairs through both reads
and writes, and verifies that an explicit read type cannot bypass declared
rounding, truncation or range limits.
[TelemetryReadCompileFail.cpp](../../tests/TelemetryReadCompileFail.cpp) supplies
twenty-seven expected compilation failures, covering static reads, invalid
bindings, enum contracts and inconsistent limit definitions.
[TelemetryJsonCheck.cpp](../../tests/TelemetryJsonCheck.cpp) sweeps
buffer lengths, checks null output and early stopping, requires a decimal-comma
locale in CI, and checks 4096 samples plus endpoints for each of F32/F64/U64/S64.
[TelemetryNumericCheck.cpp](../../tests/TelemetryNumericCheck.cpp) compares all
121 conversion pairs against an independent extended-precision oracle with
explicit truncation, checking endpoints and 1024 source samples per pair.
That oracle runs on hosts with at least 64 long-double mantissa bits.
[TelemetryEnumCheck.cpp](../../tests/TelemetryEnumCheck.cpp) covers numeric-only
data paths with enum metadata, underlying types, explicit 64-bit codes,
schema fingerprints, custom names and all output-buffer boundaries.
[TelemetryLimitsCheck.cpp](../../tests/TelemetryLimitsCheck.cpp) checks native
and custom intervals, constexpr definitions, automatic enum extrema, defaults,
inclusive boundaries, write-only validation and mandatory metadata exports.
The [test runner and instructions](../../tests/README.md) reproduce all suites,
standalone header compilation, rejected bindings and rejected fast-math flags.
[IndexCodegen.cpp](../../tests/IndexCodegen.cpp) is a compile-only ARM probe
with reproduction flags in its opening comment; use the same flags for
[ConversionCodegen.cpp](../../tests/ConversionCodegen.cpp),
[DeclaredTypeCodegen.cpp](../../tests/DeclaredTypeCodegen.cpp),
[ScalarStorageCodegen.cpp](../../tests/ScalarStorageCodegen.cpp),
[ScalarVisitCodegen.cpp](../../tests/ScalarVisitCodegen.cpp),
[EnumCodegen.cpp](../../tests/EnumCodegen.cpp) and
[LimitsCodegen.cpp](../../tests/LimitsCodegen.cpp).

CubeIDE GCC 14.3.1, C++17, Cortex-M7, `-O2` and `-Os` produce direct lookups
with no loops, helper calls or allocations. Successful lookup through a
passed view takes 14 instructions in the measured object; a fixed constexpr
view takes 13. A known ID becomes a constant address (`ldr; bx`), and a known
missing ID becomes null. These are instruction counts, not measured cycles.

On ARM32, Scalar occupies 16 bytes, Getter 12, Setter 8, FieldType 40, Field 80,
Catalog 16 and CatalogIndex 8 bytes. Native function alternatives account
for the extra 4 bytes over the previous Getter; an empty setter still occupies
its 8-byte slot. FieldType owns one native limits triple and its optional schema callback.
The probe's constant metadata resides in `.rodata`,
with no startup constructor sections and zero `.data`/`.bss`. Mutable source
values are external to the probe and still need application storage. Final
Flash/RAM placement is determined by linking.

The enum/plain U16 pairs in `EnumCodegen.cpp` use the same bounds and numeric
operations at both optimization levels, apart from table addresses/offsets
and the corresponding instruction encodings.
Known typed reads branch straight to their shared getter. Dynamic Field
reads/writes access the numeric tag at offset 16 and the getter/setter slots;
they never load the schema callback at offset 20. Reads never load limits.
The enum probe's constant
names and field arrays reside in `.rodata`, with no startup constructors or
`.data`/`.bss` storage. These observations do not assert identical cache
behavior after increasing the size of a Field.

`LimitsCodegen.cpp` confirms that bounded F32/U16 reads still branch directly
to getters. The default U16 write has no interval comparison; a custom U16
10..20 interval becomes subtraction and one unsigned comparison. F32 uses
two F32 comparisons, without double conversion. A constant rejected write
becomes `movs r0, #3; bx lr`, without a callback or runtime bound lookup.

Known native getter rows use direct calls at both `-O2` and `-Os`, with no
runtime alternative selection. Both explicit and inferred F32 read probes
reduce to a single direct branch to the getter, with no materialized Scalar,
optional or numeric conversion. This relies on the getter and metadata being
visible to the optimizer; it is not a promise for arbitrary runtime bindings.
A known write of integer 250 to F32 embeds
the float constant; a runtime U16 input needs one numeric conversion. Known
read-only and missing writes reduce to constant result returns. Bound tiny
invokers and Scalar materialization may remain; this is not a claim that
complete callbacks always inline. Runtime IDs still require bounds checks
and dispatch.

The declared-type probes at both `-O2` and `-Os` show F32/F32 and U64/U64
typed reads reduced to direct getter branches. F32-to-F64 uses one widening
instruction. F32-to-U16 performs two source-precision bounds comparisons and
one float-to-integer conversion, with no double intermediate or round/trunc
helper. U16-to-U32 needs no numeric conversion or bounds checks. The matching
F32 and U16 write probes contain no numeric conversion or memset call; the
Scalar argument and callback dispatch may still remain. These probes also
retain the required narrowing/widening when reading F64 through declared F32.

The variant and former tag/union storage probes have matching instruction
sequences, except payload/tag offsets, at both optimization levels: creation
10 bytes, guarded F32 read 16 bytes and guarded F32-to-U32 conversion 60 bytes.
Scalar remains 16 bytes with 8-byte alignment. Its construction and changing
alternatives by copy assignment also pass C++17 constant-expression checks.

Using std::variant does not require std::visit for every operation. The visit
comparison uses the same twelve alternatives as Scalar. At `-Os`, the known
F32 switch probe is `bx lr` (2 bytes); std::visit retains a 26-byte wrapper,
a stack frame and a visitor helper call. At `-O2` both known probes fold to
`bx lr`. The runtime std::visit probe uses a function table and an indirect
call; the library keeps the direct source-tag switch and guarded std::get
accesses so known paths also fold at `-Os`. These are compiled-object results,
not board cycle measurements or a general ranking of std::visit implementations.

The archived previous arbitrary-ID implementation is
in [archive/id_ranges](../../archive/id_ranges/README.md) and is not built.
