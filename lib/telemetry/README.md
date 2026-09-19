# Telemetry library

Standalone catalog, numeric conversion and JSON library for C++17.
Authors: Ruslan Kovtun (shpegun60), codexAi.

Copy this directory and its sibling `delegate` directory into a consumer,
keeping both under the same `lib` directory, and include the reusable `.pri`:

```qmake
include(path/to/telemetry/telemetry.pri)
```

The library uses C++17 and bundled [tiny_delegate v1.1.0](../delegate/README.md).
It has no Qt, STM32, RTOS, Mongoose or `basic_types.h` dependency. For a
non-qmake build, add `lib/telemetry` and `lib/delegate` to the include paths
and compile `TelemetryJson.cpp`.

Licensed under the [MIT License](LICENSE). Keep this license with copied or
redistributed library files; the sibling delegate carries its own MIT license.

## Public files

- `TelemetryScalar.h`: private variant storage for Null, Bool, F32/F64,
  U8/U16/U32/U64 and S8/S16/S32/S64, with checked accessors.
- `TelemetryCompiler.h`: shared `TELEMETRY_FORCE_INLINE` portability macro.
- `TelemetryGetter.h`: constexpr non-owning getters returning Scalar or native numbers.
- `TelemetrySetter.h`: optional write callbacks and `WriteResult`.
- `TelemetryConversion.h`: constexpr checked numeric conversions.
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
InvalidValue when conversion fails, or the owner's result after one call.
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
  round to zero. NaN/Inf remain available to floating-point owners to validate.
- Null and unknown destination tags are rejected. Compile without fast-math/finite-only
  assumptions so floating-point range checks retain their meaning.

Conversion is constexpr and shared by all field reads and writes. Equal native
types copy directly, without numeric conversion or range checks. A Scalar
already carrying the requested numeric tag is also copied directly. Integer
widening omits bounds checks where every source value fits. Float inputs stay
float for bool/integer checks; there is no universal double or int64
intermediate. Float-to-double needs just widening; double-to-float checks
finite overflow and handles NaN/Inf explicitly. Necessary range checks remain
when a runtime value might not fit.

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
type conversion and its checks. Bindings and metadata remain fixed during use.

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
{"id":1,"name":"sensor","fields":[{"i":0,"id":65536,"n":"Temperature","u":"degC","t":"f64","w":false}]}
```

Values retain named arrays such as `{"sensor":[24.5]}`. The order-sensitive
FNV-1a fingerprint includes group IDs, all four field-ID bytes, declared
metadata, setter presence (`w`) and record/string boundaries. It is a version hint, not a promise
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
thirteen expected compilation failures, covering static reads and invalid
bindings. [TelemetryJsonCheck.cpp](../../tests/TelemetryJsonCheck.cpp) sweeps
buffer lengths, checks null output and early stopping, requires a decimal-comma
locale in CI, and checks 4096 samples plus endpoints for each of F32/F64/U64/S64.
[TelemetryNumericCheck.cpp](../../tests/TelemetryNumericCheck.cpp) compares all
121 conversion pairs against an independent extended-precision oracle with
explicit truncation, checking endpoints and 1024 source samples per pair.
That oracle runs on hosts with at least 64 long-double mantissa bits.
The [test runner and instructions](../../tests/README.md) reproduce all suites,
standalone header compilation, rejected bindings and rejected fast-math flags.
[IndexCodegen.cpp](../../tests/IndexCodegen.cpp) is a compile-only ARM probe
with reproduction flags in its opening comment; use the same flags for
[ConversionCodegen.cpp](../../tests/ConversionCodegen.cpp),
[DeclaredTypeCodegen.cpp](../../tests/DeclaredTypeCodegen.cpp),
[ScalarStorageCodegen.cpp](../../tests/ScalarStorageCodegen.cpp) and
[ScalarVisitCodegen.cpp](../../tests/ScalarVisitCodegen.cpp).

CubeIDE GCC 14.3.1, C++17, Cortex-M7, `-O2` and `-Os` produce direct lookups
with no loops, helper calls or allocations. Successful lookup through a
passed view takes 14 instructions in the measured object; a fixed constexpr
view takes 13. A known ID becomes a constant address (`ldr; bx`), and a known
missing ID becomes null. These are instruction counts, not measured cycles.

On ARM32, Scalar occupies 16 bytes, Getter 12, Setter 8, Field 36,
Catalog 16 and CatalogIndex 8 bytes. Native function alternatives account
for the extra 4 bytes over the previous Getter; an empty setter still occupies
its 8-byte slot. The probe's constant metadata resides in `.rodata`,
with no startup constructor sections and zero `.data`/`.bss`. Mutable source
values are external to the probe and still need application storage. Final
Flash/RAM placement is determined by linking.

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
