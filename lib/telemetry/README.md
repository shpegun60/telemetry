# Telemetry library

Standalone field, command, numeric conversion and JSON library for C++17.
Authors: Ruslan Kovtun (shpegun60), codexAi.

Copy this directory and its sibling `magic_enum` and `delegate` directories into a consumer,
keeping them under the same `lib` directory, and include the reusable `.pri`:

```qmake
include(path/to/telemetry/telemetry.pri)
```

The library uses C++17. It has no Qt, STM32, RTOS, Mongoose or
`basic_types.h` dependency. The sibling [tiny_delegate v1.2.0](../delegate/README.md)
implements the borrowed/owned delegate slots exposed by `Telemetry.h`.
Getter/Setter and the other slot types remain independent of tiny_delegate.
For a non-qmake build, add `lib/telemetry` to the include path
and compile `abi/TelemetryAbi.cpp`. Include and compile
`serialization/TelemetryJson.cpp` for field JSON and
`serialization/TelemetryCommandJson.cpp` for command JSON, only when required. The qmake include
keeps JSON enabled by default; set `CONFIG += telemetry_no_json` before the
`include(...)` line for a core-only target.
Optional `field/TelemetryEnum.h` uses bundled [magic_enum v0.9.8](../magic_enum/README.md).
Numeric-only core headers do not include magic_enum. Field JSON uses it at
compilation to derive the policy-flag dictionary; there is no runtime reflection.

Licensed under the [MIT License](LICENSE). Keep this license with copied or
redistributed library files; the delegate and magic_enum trees retain
their upstream MIT licenses.

## Public files and layers

`Telemetry.h` is the core umbrella. It includes enum metadata, fields,
catalog/index access, signature factories, commands and the independent ABI guard; it deliberately does not
include JSON.

- `core/`: compiler/cache-line policy, shared packed IDs, Scalar and checked numeric conversion.
- `field/`: Getter/Setter, FieldType, FieldFlags, enum/limits metadata and immutable Field.
- `catalog/`: Catalog validation, direct CatalogIndex lookup and indexed field views.
- `command/`: command definitions, optional metadata, factories, flat lookup
  and packed group/index catalog lookup and views.
- `slot/`: mutable owner/function/context/delegate bindings for immutable tables.
- `abi/TelemetryAbi.h`: the exact in-memory layout tag and explicit link guard.
- `serialization/TelemetryJson.h`: schema fingerprint plus bounded schema/value JSON.

Root forwarding headers were removed. Use `Telemetry.h` or canonical paths
such as `field/TelemetryField.h` and `catalog/TelemetryIndex.h`; update old
includes accordingly. Headers under `detail/` are implementation details and
are not a stable public API.
They hold typed bounds, numeric conversion primitives and JSON writer/value
helpers; consumers should not include them directly.

## Signature-inferred fields and commands

Include `Telemetry.h` for factories, fields, catalogs and commands. Include
`serialization/TelemetryJson.h` for field JSON and
`serialization/TelemetryCommandJson.h` for command JSON. The latter also
provides the field serializer declarations.

```cpp
enum class Mode : std::uint8_t { Off, Auto, Manual };
struct Device {
    float voltage() const noexcept;
    float limit() const noexcept;
    WriteResult setLimit(float value) noexcept;
    Mode mode() const noexcept;
    WriteResult setMode(Mode value) noexcept;
    CommandResult reset() noexcept;
    CommandResult calibrate(float voltage, Mode mode) noexcept;
};
Device device; // Namespace scope; remains alive at this address.

constexpr FieldTable meterFields{
    field<&Device::voltage>("Ua", "V", device),
    field<&Device::limit, &Device::setLimit>("Limit", "V",
        device, limits(250.0f, 1.0f, 1000.0f)),
    field<&Device::mode, &Device::setMode>("Mode", "", device, limits(Mode::Auto)),
};
constexpr CommandTable meterCommands{
    command<&Device::reset>("Reset", device),
    command<&Device::calibrate>("Calibrate", device,
        arg<1>("Mode", "", Mode::Auto),
        arg<0>("Voltage", "V", 230.0f, 0.0f, 500.0f)),
};
constexpr FieldCatalogTable fields{group("device", meterFields)};
constexpr CommandCatalogTable commands{group("device", meterCommands)};
constexpr auto fieldIndex = fields.index();
constexpr auto commandIndex = commands.index();

// 1. Local compile-time position: native callback and value types.
auto voltage = meterFields.read<0>();         // optional<float>
auto wide = meterFields.read<0, double>();    // optional<double>
auto written = meterFields.write<1>(275);     // checked int -> float
auto result = meterCommands.call<1>(230.0f, Mode::Auto);

// 2. Global compile-time packed ID: routes to the same local operation.
voltage = fields.read<makeId(0, 0)>();
written = fields.write<makeId(0, 1)>(275);
result = commands.call<makeId(0, 1)>(230.0f, Mode::Auto);

// 3. Global runtime IDs supplied by a transport.
Scalar value = fieldIndex.read(receiveFieldId());
written = fieldIndex.write(receiveFieldId(), 275);
const Scalar arguments[] = {230.0f, std::uint8_t{1}};
result = commandIndex.execute(receiveCommandId(), arguments, 2);

// A local runtime position with native arguments is also supported.
result = meterCommands.call(receivePosition(), 230.0f, Mode::Auto);
```

The template numbers in a local table are zero-based entry positions, with
no group component. For example, `meterFields.read<0>()` selects `Ua`, and
`meterFields.write<1>(value)` selects `Limit` in the declaration above. Integers,
named integral constants and scoped or unscoped enums are accepted directly:

```cpp
enum class MeterField : std::size_t { Voltage, Limit, Mode };
enum class MeterCommand : std::size_t { Reset, Calibrate };
auto value = meterFields.read<MeterField::Voltage>();
auto widened = meterFields.read<MeterField::Voltage, double>();
auto status = meterFields.write<MeterField::Limit>(275);
auto action = meterCommands.call<MeterCommand::Calibrate>(230.0, 1);
```

Enum values must match the row order: names do not trigger a string lookup.
Negative, out-of-range and non-integral/non-enum positions are compile-time
errors, checked before narrowing to `size_t`, including 64-bit enums on ARM32.
Position names add no runtime work. Global typed APIs still take the packed
`makeId(group, entry)`; a local position enum does not identify its group.

`field(...)` supports the same NTTP methods/free functions, parameter function
pointers, capture-free lambdas and borrowed callable lvalues through this one factory.
`makeField` was removed; use `field(...)` entries in a `FieldTable`.
`FieldTable` retains their types, but stores only `std::array<Field, N>`.
For nonempty tables its size is exactly `N * sizeof(Field)`, with the same
alignment as Field. There is no stored definition tuple or second owner pointer.
The empty table follows `std::array<Field, 0>` storage rules.

### Runtime objects behind constant tables

Use `OwnerSlot<T>` only when an object's address is not available while defining
the table, or when all of its fields/commands need to be rebound together:

```cpp
inline OwnerSlot<Device> deviceSlot; // Empty; one pointer in RAM.
inline constexpr FieldTable lateFields{
    field<&Device::voltage>("Ua", "V", deviceSlot),
    field<&Device::limit, &Device::setLimit>("Limit", "V", deviceSlot,
        limits(250.0f, 1.0f, 1000.0f)),
};
inline constexpr CommandTable lateCommands{
    command<&Device::calibrate>("Calibrate", deviceSlot,
        arg<0>("Voltage", "V", 230.0f, 0.0f, 500.0f)),
};

// After constructing a stable application object:
deviceSlot.bind(device);
auto voltage = lateFields.read<0>();          // optional<float>, no Scalar.
auto status = lateCommands.call<0>(230.0, 1); // Native checked conversion.
deviceSlot.reset();                           // Before destroying device.
```

The same declarations with `device` instead of `deviceSlot` remain direct
bindings. Template selection adds no slot load, null check, flag or resolver
call to those ordinary objects or to free/static functions. A slot binding
loads the target once per invocation and checks it before calling the method.
The slot is one pointer (4 bytes on ARM32); Field stays 96 bytes and Command
20 bytes there. Constant tables retain their original descriptor layout and
can stay in read-only storage. No allocator or virtual resolver is introduced.

An empty slot returns Null through Scalar reads and an empty optional through
native reads. Writable fields return `WriteResult::Unavailable` for valid
inputs; field conversion/limits still precede setter invocation on every API,
so invalid inputs report `InvalidValue` even when the slot is empty. Read-only
fields still report `ReadOnly`. Command transport argument count/pointer checks
come first; then an empty slot returns `CommandResult::Unavailable` before any
numeric conversion. Raw `field.get`/`field.set` capability checks and schema do
not change with slot state: the declared callback still exists while unbound.

`bind(other)` redirects all existing descriptors to the new object. A const
target uses `OwnerSlot<const T>` and requires const-compatible methods. Derived
objects, base adjustment and normal virtual member dispatch remain supported.
For a field mixing a free function with a member callback, only the member
callback consults the slot. Slot objects cannot be copied/moved; factories and
`bind()` reject temporaries, including those passed to const-target slots.

The slot does not own or extend the lifetime of its object. Its address must
remain stable and outlive all borrowed tables/descriptors. Bind/reset and
access are not atomic: perform them before starting consumers, or externally
serialize them and keep the selected object alive until active calls finish.
Reset alone is not a way to destroy an object concurrently with an active call.

### Runtime functions behind constant tables

Use `FunctionSlot<R(Args...) noexcept>` when the callback itself is selected
after startup or can be replaced later. Pass the stable slot as a parameter:

```cpp
inline FunctionSlot<float() noexcept> voltageRead;
inline FunctionSlot<WriteResult(float) noexcept> voltageWrite;
inline FunctionSlot<CommandResult(float) noexcept> start;
inline constexpr FieldTable selectableFields{
    field("Voltage", "V", voltageRead, voltageWrite),
};
inline constexpr CommandTable selectableCommands{
    command("Start", start, arg<0>("Speed", "rpm", 0.f, 0.f, 3000.f)),
};

voltageRead.bind(&readVoltageA);
voltageWrite.bind(&writeVoltageA);
start.bind(&startMotor);
auto voltage = selectableFields.read<0>();       // optional<float>, no Scalar.
auto status = selectableCommands.call<0>(1500); // int -> float, no Scalar.
voltageRead.bind(&readVoltageB);                  // Existing table sees B.
voltageRead.reset();                             // Reads now report absence.
```

`bind()` takes the exact noexcept function-pointer type. Ordinary functions,
static methods, and capture-free `[]`/`+[]` lambdas are accepted. It does not
cast between incompatible function pointers. Input values still use the usual
checked numeric/enum conversions before the selected setter or command runs.
An enum-valued getter infers its enum dictionary as usual; Scalar-returning
getters retain the explicit `FieldType` form.

The slot exposes `bind`, `reset`, `get` and `explicit operator bool`, with no
call operator. Field/command adapters recognize it at compile time and read
one function-pointer snapshot per invocation. Empty getters return Null for
Scalar reads and `nullopt` for typed reads; empty setters/commands return
`Unavailable`. The conversion/limit/error ordering is the same as `OwnerSlot`
above. A declared setter slot remains a writable capability in the schema even
while unbound; a field with no setter remains `ReadOnly`. Getter and setter
slots can be rebound independently, and neither binding changes schema/CRC.

Native calls retain the typed signature, without a Scalar array or descriptor
dispatch. The unavoidable dynamic step is the checked function-pointer call.
Normal functions, objects and borrowed functors gain no presence check. Each
slot stores one function pointer (4 bytes on Cortex-M7); Field/Command layouts
and read-only constant tables are unchanged. The slot must outlive those tables
and cannot be copied/moved. Bind/reset and all access require external
serialization, just as for `OwnerSlot`. A slot does not own captured state;
use an ordinary borrowed functor or `OwnerSlot` for callbacks needing an object.

The complete [slot family](slot/README.md) also provides
`ContextFunctionSlot`, `DelegateRefSlot` and `DelegateSlot` for context callbacks,
borrowed callables and owned captured lambdas. All five public slot headers now
live under `slot/`; direct includes of the former `core/TelemetryOwnerSlot.h`
or `core/TelemetryFunctionSlot.h` must use that directory. `Telemetry.h` includes
the entire family. `available()` complements `operator bool()` on every slot.

### Native field access

Native `read<I>()` returns `optional<Scalar::NativeType<tag>>`; an enum returns
its numeric representation. `read<I, T>()` uses the shared checked native
conversion. Reads ignore write limits. Native `write<I>()` converts directly
to the destination number, applies the same finite/bounds validator as dynamic
Field writes, then calls the concrete setter. The existing descriptor payload
supplies the owner or exact function pointer. Runtime function-pointer forms
still need their native indirect call when the target is not known to the
compiler; they do not construct Scalar for numeric inputs.

Scalar-returning callbacks require explicit FieldType. They, and manually
adapted `field(Field{...})` entries, use the ordinary Field path: `read<I>()`
returns Scalar and `read<I, T>()` returns optional<T>. This preserves declared
type normalization before requested widening or truncation. Scalar write
inputs remain supported; only those inputs need Scalar extraction.

`group(name, table)` borrows a stable lvalue FieldTable or CommandTable. The
global catalog tables own their ordinary descriptor arrays and one typed
table pointer per group. They route compile-time IDs to the selected table
without constructing a dynamic index. `.index()` returns the ordinary borrowed
runtime view; global runtime convenience methods use that same view. The
tables are non-copyable/non-movable; borrowed owners, closures, strings and
local tables must outlive their consumers. Extracting views from temporaries
is rejected. None of these layers stores an ID or allocates memory.
Metadata values such as `limits(...)` and `arg<N>(...)` are copied, but their
name/unit pointers do not copy text. A temporary string's `c_str()` is not a
valid persistent label; use literals or storage that remains alive and stable.

`field` produces a temporary typed definition. `FieldTable` materializes the
same concrete RW32 `Field`; there is no additional runtime wrapper. The native getter determines `numericType<T>()` or
`enumType<E>()`. Its typed setter must accept that exact C++ value type and
return `WriteResult`; both callbacks must be `noexcept`. A different enum
with the same underlying integer is rejected. Getters and command parameters
must return/take values, without reference, pointer, string or long-double
parameters. Const and lvalue-qualified methods are supported. Owners are
borrowed lvalues, including runtime objects; temporary owners are rejected.

`limits(default)` changes only the initial metadata. The three-argument form
is `limits(default, min, max)`. Values must match the signature's exact C++
type; use `250.0f` for float. Invalid bounds/defaults reject constexpr
definitions; runtime construction of an invalid definition terminates, as
with `numericType`. Bounds are inclusive and apply after checked conversion.
Read operations keep the established rule: normalize to the declared type,
without checking write limits. Defaults never mutate an owner automatically.

The ordinary Field constructor remains available for explicit type adaptation
or empty getters. A Scalar-returning getter cannot imply its payload type;
use `field<&Device::readScalar>(name, unit, ScalarType::F32, device)`
(and, optionally, a `WriteResult(const Scalar&) noexcept` setter). For named
borrowed callables, use `field(name, unit, ScalarType::F32, getter)` or
the corresponding `getter, setter` overload. The callable lifetime rules below
still apply.

Free/static functions need no owner. Capture-free numeric getter and setter
lambdas also work directly, with or without unary `+`:

```cpp
constexpr FieldTable callbackFields{
field("Ua", "V",
    []() noexcept { return device.voltage(); }),

field("Limit", "V",
    []() noexcept { return device.limit(); },
    [](float value) noexcept { return device.setLimit(value); },
    limits(250.0f, 1.0f, 1000.0f)),
};

// A C++17 named lambda pair can be used as template targets.
constexpr auto readLimit = +[]() noexcept { return device.limit(); };
constexpr auto writeLimit = +[](float value) noexcept {
    return device.setLimit(value);
};
constexpr FieldTable limitFields{field<readLimit, writeLimit>("Limit", "V",
    limits(250.0f, 1.0f, 1000.0f))};

// Capturing/stateful callbacks are borrowed from named, stable lvalues.
void useRuntimeDevice(Device& runtimeDevice)
{
    auto capturedRead = [&runtimeDevice]() noexcept { return runtimeDevice.limit(); };
    auto capturedWrite = [&runtimeDevice](float value) noexcept {
        return runtimeDevice.setLimit(value);
    };
    const FieldTable capturedLimit{field("Captured limit", "V",
        capturedRead, capturedWrite, limits(250.0f, 1.0f, 1000.0f))};
    // capturedLimit must not outlive capturedRead, capturedWrite or runtimeDevice.
}

CommandResult saveConfig() noexcept;
constexpr CommandTable systemCommands{command<&saveConfig>("Save")};

// Stateful callable objects are borrowed as named, stable lvalues.
constexpr auto resetLambda = []() noexcept { return device.reset(); };
constexpr CommandTable lambdaCommands{command("Reset lambda", resetLambda)};
```

Capture-free callbacks take the native function-pointer path, including inline
`[]` and `+[]`. Capturing lambdas and stateful functors take the borrowed-object
path and therefore must be named lvalues that outlive every copied `Field`.
For an enum-returning capture-free lambda, use a named template target as in the
lambda pair; the template form preserves enum identity. Commands have the same
stable-lvalue rule for borrowed callable objects. Generic, overloaded or
throwing call operators and temporary capturing closures are rejected. No
factory stores a pointer to a temporary closure.

Command IDs form a separate logical space. For one flat zero-based array,
`CommandIndex` clips its count to the entry capacity and uses one bounds check.
`CommandCatalogIndex` uses the same packed 16-bit group/index arithmetic as
fields: group and entry positions are their identities, and lookup uses
two bounds checks and direct indexing. `CommandCatalog::name` may use paths such
as `Motor/Control`, so field and command schemas can describe the same UI
section without storing a path in every Command. Both indexes borrow stable
definition arrays; temporary arrays are rejected. A Command is naturally 20 bytes on
ARM32 (40 on the tested x64 ABI). Its handler already knows the
argument count/types, so those are not duplicated in the descriptor. Commands
without decoration store no parameter array. Prebuilt `commandArgs(...)` can also be passed to
`command(...)`; the table copies that metadata into its own stable storage.
The old public `makeCommand` factory is removed; use `CommandTable` entries.

`command(...)` creates a value specification without internal pointers.
Direct `CommandTable{...}` construction owns its metadata tuple and ordinary
Command descriptors. Those descriptors point into the owned metadata, so the
table is non-copyable and non-movable. `data()`, `operator[]` and `index()` are
lvalue-only. `size()` may be used on a temporary because it returns a value.
Copying a `Command` descriptor out of the table does not copy its referenced
metadata or owner; the table must still outlive that copied descriptor's use.
In C++17 construct CommandTable directly: returning a self-referential table
from a factory is not a portable constant expression. `CommandCatalogTable`
is now the multi-group registry shown above, not an owning single-group wrapper.
There is no fixed arity limit beyond compiler and application resources.

An owning command table provides three execution levels:

```cpp
table.call<1>(voltage, mode);         // Compile-time position, native arguments.
table.call(runtimeIndex, voltage, mode); // Runtime position, native arguments.
index.execute(id, scalarArgs, count); // Runtime ID and runtime Scalar values.
```

`call<Index>()` selects the definition and target signature at compilation. The
owner address and metadata values can still come from runtime construction. Its
argument count must match, and inputs must be supported numeric or enum values.
Types may differ from the signature: `call<1>(230.0, 1)` converts `double` to
`float` and `int` to `Mode` directly, without constructing any Scalar. Identical
types keep the existing direct path. Mixed types use a local tuple of native
target values and the shared checked number conversion; there is no common
`double` intermediate. References/cv qualifiers on input lvalues are removed.
An invalid index, arity or unsupported type is a compile-time error.
Overflow, non-finite values and values outside target enum/custom bounds return
`InvalidValue`, with no callback. Fractional inputs truncate toward zero when
converted to integer or enum codes. Limits apply after conversion to the target
type, just as they do on the Scalar transport path. All arguments must pass
before one target call. This path bypasses the erased Command invoker entirely.
Ordinary nonvirtual template targets can compile to direct calls; virtual
methods retain normal C++ dispatch unless the compiler can devirtualize them.

`call(runtimeIndex, ...)` uses the same native conversions and validation,
but emits typed branches because the local zero-based table position is known
only at runtime. A selected definition with another argument count returns
`ArgumentCountMismatch`; an out-of-range position returns `NotFound`. The
generated dispatcher has one range check and emits comparisons/invocations
only for definitions with the supplied argument count. Other arities are
removed by `if constexpr`. Each selected branch knows the destination types
and converts directly without Scalar, subject to the same virtual-method rule.
Generated code grows with the number of matching arities. With automatic
conversion this can include more definitions than exact-type filtering did;
the runtime native overload trades Flash for dispatch speed.

`CommandIndex::execute()` and `CommandCatalogIndex::execute()` remain the fully
dynamic transport APIs. They accept IDs plus a borrowed Scalar array, perform
checked Scalar-to-native conversion and use the descriptor's erased invoker.
The corresponding `call(id, values...)` convenience APIs first construct that
Scalar array. Fractional numeric inputs truncate toward zero under the shared
conversion policy. Both dynamic and native paths then use the same native
finite/range/enum validator, so their accepted value sets cannot drift.

`CommandCatalogTable::call<Id>()` routes a packed ID to a direct local call.
Its `call(id, values...)` overload uses the dynamic Scalar convenience path;
`execute(id, args, count)` accepts transport values directly. All arguments
must be present in every path: defaults are schema/UI values. Enum parameters
use their inferred numeric interval; unnamed gaps remain allowed. Automatic
enum discovery has the same magic_enum range contract as `enumType<E>()`. The
callback must return `CommandResult`; results are passed through unchanged.

| Result | Meaning |
|---|---|
| `Executed` | Owner completed the action synchronously |
| `Accepted` | Owner queued a copied request; not yet completed |
| `NotFound` | ID is outside the positional command bounds |
| `Unavailable` | Command has no handler |
| `ArgumentCountMismatch` | Runtime argument count differs from the target |
| `InvalidValue` | Conversion, finite-value or range validation failed |
| `Busy` / `Failed` | Owner could not execute the action |

The library adds no queue, locking, persistence or asynchronous completion.
An owner that queues work must copy values. A failure returned by the owner
does not imply rollback of its own side effects.

`writeSchema(flatActions, buffer, size)` emits the legacy flat
`{"schema":"...","commands":[...]}` document. A `CommandCatalogIndex` emits
`{"schema":"...","commandCatalogs":[{"id":0,"name":"device","commands":[...]}]}`.
Grouped commands contain `i`, packed `id`, `n` and `params`; the flat format
contains `id`, `n` and `params` without a separate `i`. Parameters have `i`, numeric `t`,
`min`, `max`, `default`, optional names/units and enum dictionaries. Absent
metadata omits `n`/`u`, allowing a UI to display arg0/arg1. Schema generation
never invokes command handlers. `JsonInt64Mode::String` also applies to command
metadata. Logical fingerprints ignore that wire choice and callback addresses.
Field and command schemas remain separate documents and logical ID spaces.
`commandNamesUnique(commands, count)` and
`commandCatalogNamesUnique(catalogs, count)` provide optional constexpr checks
for null or duplicate command/group names, analogous to the field helpers.

**For stable wire schemas, use fixed-width integers** (`std::uint16_t`,
`std::int32_t`, and so on), `float`, `double`, `bool`, and enums with an explicit
fixed underlying type. Types such as `long`, `size_t`, `wchar_t` and an enum
without a fixed underlying type may infer a different Scalar type on ARM32 and
64-bit hosts. They remain supported for local use, but identical source alone
does not guarantee an identical cross-platform schema for those types.

## Field policy flags

Flags describe field policy independently of callback availability. Add them
after any `field(...)` factory; no additional factory overload is needed:

```cpp
constexpr FieldTable settingsFields{
    field<&Device::limit, &Device::setLimit>("Limit", "V", device,
        limits(230.0f, 0.0f, 500.0f))
        .withFlags(FieldFlag::Persistent),
};
const Field& setting = settingsFields.data()[0];
bool saveAndRestore = setting.persistent();
bool hasSetter = setting.writable();
FieldFlags policy = setting.flags();
```

`withFlags()` returns the same definition type and replaces its policy mask;
native typed dispatch is preserved. The old declaration defaults to no flags.
`FieldFlags{FieldFlag::Persistent}` is the explicit wrapper form. `operator|`
combines flags/masks, and `contains()` or `field.has()` checks that **all**
requested bits are present. The empty mask is always contained; use `empty()`
to test for no flags. `FieldFlags::fromRaw(uint32_t)` explicitly preserves
unknown wire bits; ordinary integers cannot implicitly become a policy.

The wire contract currently defines `None = 0` and `Persistent = 1u << 0`.
Existing bits will not be renumbered; consumers ignore unknown bits.
Persistent means save **and** restore, so both getter and setter capabilities
must exist. Invalid constexpr definitions fail compilation; invalid runtime
construction terminates with `std::abort()` before publishing the descriptor.
A declared late-bound slot may be empty: its adapter still supplies that
capability and reports unavailability when called. Flags do not affect
read/write instructions, apply defaults, or provide a storage backend.

Schema always exports a numeric `"f"` mask, including zero. `"w"` remains
setter capability, independently of flags. All four flag bytes enter the
schema fingerprint in little-endian order. Values JSON remains unchanged.
The schema header describes known bits once, using `magic_enum` flags reflection
over `FieldFlag`; there is no second maintained list of numeric codes/names.
Zero and composite aliases are omitted. Named powers of two across all 32 bits
are reflected, independently of the usual small-integer enum scan range.

## Catalog and parameter traversal

Global owning tables and their borrowed indexes expose symmetric indexed views:

```cpp
for (auto catalog : fields.catalogs()) {
    for (auto entry : catalog.fields()) {
        const FieldId id = entry.id();
        const EntryOffset localPosition = entry.index();
        const Field& descriptor = entry.field();
        // catalog.index(), catalog.name(), descriptor.flags(), ...
    }
}
for (auto catalog : commands.catalogs()) {
    for (auto entry : catalog.commands()) {
        const CommandId id = entry.id();
        const Command& descriptor = entry.command();
        descriptor.forEachParameter([](const CommandParam& parameter) noexcept {
            // Consume parameter.index/name/unit/type here.
            return true; // false stops this command's parameter traversal.
        });
    }
}
```

IDs are computed from group and entry positions; descriptors still contain no
stored ID. Reserved positions are included, and empty groups remain visible.
The iterator counter is `size_t`: position 65535 is valid and end position
65536 does not wrap. Raw descriptor views cap extents at 65536, just like
runtime lookup, and normalize null storage to an empty range. A non-null
pointer must refer to an array of at least the supplied extent.

Local FieldTable/CommandTable, Catalog/CommandCatalog and indexes also provide
const `begin()/end()/size()/empty()` for ordinary descriptor traversal. Global
table/index raw iteration yields catalog descriptors; use `catalogs()` when
IDs are needed. The indexed iterators return small views by value, support
`operator->`, and have input-iterator semantics.

All ranges borrow their tables, descriptors and strings. Copying a view does
not extend the owners' lifetimes. A nested range copies its group context, so
it does not borrow the temporary catalog view. Borrowed indexes may themselves
be temporary if their backing storage survives. Extracting `begin()/end()` or
`catalogs()` from a temporary owning table is rejected; keep that table alive.
Internal ranges reuse bounds already normalized by their catalog/index; the
private construction path cannot be used to bypass public pointer/count checks.

`Command::forEachParameter()` adapts `describeParameters()` synchronously with
no parameter array or visitor copy. The visitor must accept `const CommandParam&`
and return a non-throwing bool-convertible result. It returns true after a
complete traversal (including a defined zero-argument command), false on early
stop, an undescribed/reserved command, or a null function-pointer visitor.
Parameter references last only for the callback; copy a parameter if needed
later, and keep its borrowed labels/enum metadata alive as well.
The callback context points directly at the visitor and restores its exact
const/volatile qualifiers. Function references are adapted through a local
function-pointer object, never by converting a function address to `void*`.

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

`visit()` exposes all twelve native alternatives through the usual `std::visit`
contract. Name a value before visiting it:

```cpp
Scalar value = fieldIndex.read(receiveFieldId());
value.visit([](const auto& native) {
    using T = std::decay_t<decltype(native)>;
    if constexpr (std::is_same_v<T, std::monostate>) {
        // No value.
    } else {
        // native has the exact bool/integer/floating type, with no conversion.
    }
});
```

A mutable lvalue Scalar passes mutable alternatives; a const lvalue passes
const alternatives. The visitor must handle Null (`std::monostate`) as well
as every numeric alternative, and return the same type/category from every
branch, as required by `std::visit`. Enum-valued fields expose their underlying
number. Callback exceptions propagate when exceptions are enabled. References
returned or retained by the visitor borrow the Scalar's active alternative;
reassignment or destruction can invalidate them. Both mutable and const
temporary Scalar visitation are rejected at compilation.

The old public `type` member becomes `type()`; old union members become the
accessors above. The in-memory layout changes, even though the measured ARM
size stays 16 bytes: rebuild consumers and never use the object bytes as a
wire or persistent format. Factories, tag numbers and JSON formats remain
compatible. `Scalar::NativeType<ScalarType::F32>` is `float`; this mapping is
derived directly from the variant alternatives and also drives inferred reads.

Plain default declarations work without braces:

```cpp
Scalar value;    // Null.
Field field;     // name/unit "", declaredType Null, get/set nullptr.
Catalog group;   // name "", fields nullptr, count 0; identity comes from position.
```

The same defaults apply in `constexpr` declarations and when trailing
arguments are omitted from a Field initializer. Its constexpr constructor
retains the order `{name, unit, declaredType, getter, setter}`.
Field is no longer an aggregate: designated initializers are not supported.
Copy/move construction and public metadata reads remain available. Field
members are const; assignment and individual definition edits are rejected.
Pass names, types and bindings to the constructor. An empty getter returns Null.

The RW32 Field occupies 96 bytes on Cortex-M7, as did B32. Getter and its
cached numeric type use the first 32-byte line; Setter, write type/flags and
all bounds use the second. Defaults and the enum description occupy the third;
names fill unused space in the first. Read/write touch only their
respective Field metadata line, in addition to index, owner and callback data.
See the [RW32 measurement report](../../tests/field_layout/h7s/RW32_RESULTS.md).

Field alignment comes from `TELEMETRY_CACHELINE_BYTES` and the constexpr wrapper
`telemetry::cacheLineBytes`. Override globally with, for example,
`-DTELEMETRY_FORCE_CACHELINE=64`. Cortex-M defaults to 32; ordinary desktop
targets default to 64; Apple ARM defaults to 128. These are compile-time
policies based on target macros, not a hardware query. Unknown targets need
an explicit setting if the default does not match their cache geometry.
No Qt or SPSC headers are required. Invalid powers/sizes and conflicting
overrides fail compilation. Field also rejects a line too small to contain
its complete write contract on the target ABI. Command deliberately remains a
natural 20-byte ARM descriptor after ID removal. The historical 24/32-byte
measurements describe ABI 5; the ABI-6 natural/padded comparison is recorded in
[the positional table checks](../../tests/position_tables/README.md).

The setting must be identical in every translation unit and static library
inside one executable. Changing it changes Field alignment, member offsets and
possibly sizeof(Field). Separate executables may choose independently: a
32-byte STM32 build and a 64-byte Qt host exchange JSON/IDs, not raw Field
objects, so their in-memory layouts need not match. With the 64-bit Qt/MinGW
ABI and default 64-byte lines, Field is 128 bytes; the Cortex-M7 default
remains **96 bytes**, aligned to **32**.

### Field ABI migration and storage

`telemetry::telemetryAbiVersion` is **7**. The four policy bytes now occupy
former Field padding, even though descriptor sizes have not grown. ABI 6
objects must be rebuilt: the exact signature now includes flag offset, size
and alignment, and compiled entry points reject the old ABI tuple.

The earlier ABI 6 migration removed stored `id` from Field, Command, Catalog
and CommandCatalog. Their
constructors and factories no longer accept it. A global CommandCatalogTable
is constructed from groups; an individual group's owner is CommandTable.
Remove declaration IDs, preserve table order, and clean-rebuild all consumers.

On ARM32 Field remains 96 bytes/aligned to 32. Getter/readType remain at 0/8;
name/unit are at 12/16, flags at 20, Setter/FieldType remain at 32/40. Catalog and
CommandCatalog are 12 bytes. Natural Command is 20 bytes. The exact ABI tuple
covers all remaining members, nested storage, sizes and alignment, including
the cache-line policy. Its unhashed tuple is part of compiled JSON and explicit
`requireTelemetryAbi()` link symbols. Mixed builds fail when they use those
entry points. Inline-only consumers must call the explicit anchor at a module
boundary if they need that link-time check. The diagnostic hash is not a wire
version or a collision-based substitute for the link tuple.

The guard adds no instruction to Field lookup/read/write. It compares no value
at runtime. Host and Cortex-M7 negative link checks compile opposite cache-line
settings and require the final link to fail. They also reject frozen ABI 6
callers against each ABI 7 core/field-JSON/command-JSON archive. Separate executables still use
their own signatures normally.

On Cortex-M7 the current Field remains 96 bytes/aligned to 32. Setter stays at
offset 32 and declaredType at 40; Getter shrinks to 8 bytes and readType moves
to offset 8. An equal sizeof does not make layouts binary-compatible. **Clean and rebuild every
translation unit and static library that uses these headers.** Mixing stale
objects built against different layouts is invalid. Never persist or transmit
the raw bytes of Field, Scalar, Getter, Setter or command objects.

Positional construction keeps its documented order. Member-order-dependent
structured bindings are source-incompatible: the declaration order is now
`get, readType, name, unit, flags_, set, declaredType`. Prefer named member access,
including `flags()` for policy metadata. C++20
designated initialization must be replaced with positional construction.

Ordinary `Field[]`, `std::array<Field, N>` and conforming C++17 allocation
provide the required alignment. Custom allocators, arenas, placement storage
and linker sections must honor `alignof(Field)` for the base and
`sizeof(Field)` for each row's stride. For one placement-constructed object:

```cpp
#include <cstddef>
#include <new>
alignas(Field) std::byte storage[sizeof(Field)];
Field* field = ::new (static_cast<void*>(storage)) Field{"value", "", ScalarType::F32};
// Publish views only after construction; destroy them before reusing storage.
field->~Field();
```

A plain byte buffer or `malloc` is not guaranteed to provide this extended
alignment. Do not pack these objects. A local `Field[20]` occupies 1920
bytes on ARM32 (as in B32; older 80-byte rows needed 1600), plus possible stack realignment overhead.
Prefer static constexpr definitions when owners and bindings permit them;
budget runtime tables explicitly when they live on a task's stack.

Build the complete definition through the constructor. Field enforces its
immutability, including the read tag duplicated from declaredType. Existing
`field.declaredType.minimum()` and `.hasEnum()` calls keep their syntax.
Copy construction works; `field = other` and `field.declaredType = ...` do not.
To change a definition, build a replacement table and its Catalog/view with
the required lifetimes. Values inside bound owners remain mutable under the
caller's synchronization contract. Catalog retains its capped positional bounds.

## Write limits and defaults

`FieldType` always supplies `minimum()`, `maximum()` and `defaultValue()` as
Scalar values of the declared numeric type. `ScalarType::F32` in a row still
works: its limits are `numeric_limits<float>::lowest()` and `max()`, and its
default is zero. All integer types use their native extrema; Bool uses
false/true with default false. Null and unknown types have Null metadata.
Floating bounds use `lowest()`, not `min()` (which is a small positive number).

Use a constexpr factory for custom definitions:

```cpp
constexpr auto voltage = numericType<float>(230, 0, 300); // default, min, max.
constexpr auto count = numericType<std::uint16_t>(15, 10, 20);
constexpr auto ordinary = numericType<double>(); // Native extrema, default 0.
constexpr auto chosenDefault = numericType<float>(230); // Native extrema, default 230.
constexpr auto nonnegative = numericType<float>(230, 0); // Native max, default 230.
constexpr auto anotherDefault = count.withDefault(12);
constexpr auto anotherRange = count.withLimits(0, 100, 50);
```

`numericType<T>` takes **default, minimum, maximum**, in that order. Either
bound can be omitted from the end; omitted bounds use the native extrema.
The zero-argument overload directly constructs the same descriptor as the
corresponding ScalarType, with `restricted_ = false`. Changing only the
default also leaves native bounds unrestricted. `withLimits` keeps its
explicit `minimum, maximum, default` order.

Arguments undergo checked conversion to the declared type, using
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
`default`; Null metadata exports null properties. All three values participate
in the schema fingerprint.
For ordinary numeric fields, a **null bound means the native endpoint of `t`**
(finite for F32/F64). Each bound is compacted independently, including an
explicitly supplied native endpoint. Resolve null using `t` while retaining
the normal write checks:

```cpp
numericType<float>();             // "min":null,"max":null,"default":0
numericType<float>(230);          // "min":null,"max":null,"default":230
numericType<float>(230, 0);       // "min":0,"max":null,"default":230
numericType<float>(230, 0, 300);  // "min":0,"max":300,"default":230
numericType<std::uint16_t>();     // "min":null,"max":null,"default":0
numericType<bool>();              // "min":false,"max":true,"default":false
enumType<Mode>();                 // "min":0,"max":2,"default":0 for Off=0, Auto=1, Manual=2.
```

Internal `minimum()`/`maximum()` still return exact Scalars. Defaults remain
explicit, even when equal to a native endpoint. **Enum and Bool bounds are
always explicit**, including enum extrema equal to the underlying type's
native limits. An enum's omitted default remains its smallest named code.
Custom F32 bounds and defaults use 17 significant digits for the promoted
double, so clients parsing JSON
as double can write them back exactly. The Qt display keeps the raw JSON
text, preserving U64/S64 metadata digits too. Clients must resolve a null
numeric bound from `t`; the schema fingerprint changes with this format.

FieldType separates a 16-byte native bounds payload from its cold default
Scalar and enum callback. Bounds have private typed union alternatives: the
same constructor selects the active alternative and numeric tag, and whole
descriptor copying preserves them together. There is no type punning or
independent public tag/payload mutation. Scalar itself retains std::variant.
On Cortex-M7 FieldType is 48 bytes and Field remains 96. There is no heap
allocation or borrowed pointer to temporary bounds. Constant tables can remain
in Flash; runtime definitions occupy their owner's storage.

## Enum dictionaries for schemas

An enum describes names for a numeric field. Its underlying type determines
the existing Scalar type; there is no `ScalarType::Enum` or enum alternative
in the variant. Include `Telemetry.h` where enum fields are defined:

```cpp
enum class Mode : std::uint16_t { Off, Auto, Manual };
Mode mode = Mode::Auto; // Owner's storage, with a stable lifetime.
constexpr auto readMode = +[]() noexcept { return mode; };
constexpr auto writeMode = +[](Mode value) noexcept {
    mode = value;
    return WriteResult::Applied;
};
constexpr FieldTable settingsFields{
    field<readMode, writeMode>("Mode", "", limits(Mode::Auto)),
};
constexpr FieldCatalogTable fields{group("settings", settingsFields)};
constexpr auto index = fields.index();
auto number = fields.read<makeId(0, 0)>(); // optional<uint16_t>.
auto result = index.write(makeId(0, 0), 100); // InvalidValue: outside 0..2.
```

Callbacks use their actual enum type; public field reads expose native numbers.
During compilation,
`enumType` derives min/max from its listed codes and chooses the smallest
code as default. `enumType<Mode>(Mode::Auto)` overrides that default while
retaining automatic bounds. `.withDefault(number)` and `.withLimits(min, max,
default)` also work and retain the dictionary.

Writes check only the resulting numeric interval. Gaps between named codes
remain writable: codes 0 and 10 admit 5. Membership/semantic validation belongs
to the owner. Reads ignore the write interval and may publish code 100.

`writeSchema()` emits the numeric type and an extra property:

```json
{"t":"u16","min":0,"max":2,"default":1,"enum":{"0":"Off","1":"Auto","2":"Manual"}}
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

Signature factories use the same explicit dictionary through `enumSpec` while
retaining compile-time type matching:

```cpp
constexpr auto codes = enumSpec<Code::Ready, Code::Last>(Code::Ready);
constexpr FieldTable codeFields{field<&Device::code, &Device::setCode>(
    "Code", "", device, codes)};
constexpr auto commandMetadata = commandArgs(arg("Code", "", codes));
```

The optional argument selects the default. Without it, the smallest listed
numeric code is used. Values must all have the getter/parameter's exact enum
type. The dictionary affects schema metadata and the accepted numeric extrema;
the hot lookup/read/write path still sees only the underlying Scalar type.

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
convertible to `ScalarType`. Existing positional rows containing `ScalarType::F32`
and comparisons with ScalarType keep working. Use `ScalarType type = field.declaredType`
when a concrete enum is needed; `auto` deduces FieldType. A standalone
FieldType builder can be reassigned a ScalarType to clear its dictionary and
restore native limits/defaults. A constructed Field holds its descriptor const.
Data paths never read the schema callback.

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
constexpr FieldTable meterFields{
    field("Ua", "V", []() noexcept { return meter.voltage; }),
    field("Ia", "A", +[]() noexcept { return meter.current; }),
};
constexpr FieldTable sensorFields{
    field<&Sensor::temperature>("Temperature", "degC", sensor),
};
constexpr FieldCatalogTable fields{
    group("meter", meterFields),
    group("sensor", sensorFields),
};
constexpr auto index = fields.index();
const FieldId id = makeId(1, 0); // 65536: sensor temperature.
const Field* descriptor = index.find(id);
Scalar value = index.read(id);
```

For this static example, source addresses must be usable in constant
expressions. The sources themselves may be mutable. An instance-bound table
uses the same constructors without `constexpr`; the owning object must keep
its address. The demo uses namespace-scope owners and constexpr tables; the
capturing callback example above shows a local runtime binding.

The lookup splits the ID with `id >> 16` and `id & 0xffff`, checks the group
against the group count, then checks the position against that
catalog's field count. It computes the address directly. There is
no scan, binary search, hash, allocation, or per-field pointer table. Only
actually present groups and fields occupy storage; the full 16-bit capacity
is never reserved automatically. The lookup body is forced inline on
GCC/Clang/MSVC, including size-optimized builds on the verified ARM compiler.

`groupOf(id)` and `indexOf(id)` expose the two components. `CatalogIndex`
provides `find(id)`, `read(id)`, `read<T>(id)`, `write(id, value)`,
`catalog(group)`, `data()` and `size()`. `catalog()`
returns null for a group outside the catalog bounds. The view stores only
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
constexpr Field count{"Count", "", ScalarType::U16,
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

For compatibility with manually declared Field arrays, when the metadata has
static storage and is constexpr, bind the array into
the index's C++ type to enable an inferred destination:

```cpp
// Compatibility only: catalogs is a namespace-scope constexpr Catalog array.
constexpr auto fixed = CatalogIndex::bind<catalogs>();
auto ua = fixed.read<makeId(0, 0)>();          // optional<float>, F32 metadata.
auto temperature = fixed.read<makeId(1, 0)>(); // optional<double>, F64 metadata.
auto wide = fixed.read<makeId(0, 0), double>(); // optional<double>, checked F32 first.
auto result = fixed.write<makeId(0, 4)>(275);   // Input type deduced, normalized to field type.
auto converted = fixed.read<float>(makeId(1, 0));
Scalar value = fixed.read(makeId(1, 0));
```

The returned type is `StaticCatalogIndex<catalogs>`. The compiler determines
the field and native result type; its value is still read at runtime. The
same type retains `find`, runtime-ID reads, writes and serialization support.
It has no mutable binding and implicitly supplies the shared const
`CatalogIndex` view to existing consumers. `bind` rejects non-constexpr
metadata immediately. `read<Id>()`, `read<Id, T>()` and `write<Id>(value)`
reject IDs outside the catalog bounds at compilation. Inferred `read<Id>()`
also rejects Null metadata and unknown declared types. Empty or unavailable
getters still need an optional result even for a valid compile-time ID. A
known read-only field returns `WriteResult::ReadOnly`, matching runtime-ID
semantics; the compiler reduces that path to the constant result.

An ordinary `CatalogIndex{catalogs}` stores only a pointer and count; its
table is not part of its C++ type. Use `read(id)` or `read<T>(id)` on that view.
The demo derives this runtime view from its typed FieldCatalogTable.

## Position identity and lifetime

**Reordering or deleting table entries changes their public IDs.** The first
group is 0, the first entry is 0. Insertion shifts every later ID. An entry
has no stored group identity: the same local table can be exposed in two
groups, with IDs computed separately in each traversal.

Use placeholders when retiring a published entry:

```cpp
constexpr FieldTable values{
    field<&readVoltage>("Ua", "V"),
    reservedField(),                  // Keeps position 1 unavailable.
    field<&readCurrent>("Ia", "A"),
};
constexpr CommandTable actions{
    command<&reset>("Reset"),
    reservedCommand(),               // Keeps position 1 unavailable.
};
```

Reserved fields return Null/empty optional and ReadOnly on write; reserved
commands return Unavailable. They remain visible as empty schema rows, keeping
the wire positions stable. An empty group likewise keeps its position. Name
uniqueness checks are optional; multiple empty placeholder names are expected
to fail a strict uniqueness check.

Local/global typed tables reject more than 65536 entries/groups at compilation.
Raw descriptor views cap counts at 65536 and treat null arrays as empty. There
are no ID-prefix scans. Missing positions return NotFound or an unavailable
read; no request clamps to another entry. Metadata is immutable and borrowed
views refer to stable arrays. Catalogs themselves remain copy-constructible.

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
supported. `field` can borrow a named capturing lambda or stateful functor;
that callable and everything it captures must remain alive at the same address.
Temporary closures are rejected.
Const objects work with const methods. Empty getters, including a typed null
function pointer, return Null. Sources need no telemetry return type:

```cpp
double Sensor::temperature() const noexcept { return temperature_; }
bool Sensor::enabled() const noexcept { return enabled_; }
float readUa() noexcept { return meter.voltage; }

constexpr FieldTable fields{
    field("Ua", "V", []() noexcept { return meter.voltage; }),
    field("Ia", "A", +[]() noexcept { return meter.current; }),
    field("UaFunction", "V", readUa),
    field("UaAddress", "V", &readUa),
    field<&readUa>("UaTemplate", "V"),
    field<&Sensor::temperature>("Temperature", "degC", sensor),
};
```

Let the object type be deduced in `bind<&Owner::method>(owner)`. Temporary
objects are rejected, including when a caller explicitly supplies `const Owner`
as the template type. Explicit reference template types are rejected too.
Binding an existing const object is supported; the owner must remain alive
at the same address until the final invocation. Returning a binding to a local
object or manually creating a dangling reference still violates that contract.

Explicit `Scalar::fromF32(...)` and typed `-> Scalar` returns remain valid.
Getter initially packages its result using the native C++ type; `Field::read`
normalizes it to the field's declared type. Any supported numeric/bool source
type can therefore back any numeric/bool field, subject to the conversion
policy and value range. JSON and the Qt display use the same normalized read.
Calling `field.get()` directly is a low-level callback invocation that retains
the source type; use `read()` or `read<T>()` to apply the field contract.

Getter is a trivial two-word value: one union payload containing the exact
native function pointer or borrowed object pointer, plus one generated invoker.
It is 8 bytes on ARM32. The union is read only through the invoker selected by
the constructor; no incompatible function-pointer conversion or type punning
is used. Compile-time function/method bindings and native parameter-form
callbacks stay constexpr in C++17. Runtime descriptor calls use an erased
invoker; a stored runtime function pointer can add another indirect target
transfer. Known rows and targets can be folded by the compiler. At the low-level
Getter constructor, conversion objects may throw before an existing getter is
replaced; invocation remains noexcept. The higher-level `field(...)` factories
require function-pointer conversions to be noexcept. Copying a getter copies
its binding, not its source.

`Getter::bindContext<&adapter>(owner)` is the low-level adapter form used by
typed factories when a generated function must receive a stable runtime owner.
The adapter accepts `Owner&`, is `noexcept`, and returns a Scalar-compatible
value, such as Scalar or a supported native number. The owner must remain alive
at the same address, and temporary owners are rejected.

## Optional writes

Field's optional final constructor argument is `Setter`, defaulting to nullptr.
Rows containing only name, unit, type and getter remain read-only. Setter uses the same trivial payload/invoker representation
and is 8 bytes on ARM32. It can retain the exact native typed callback pointer
or a borrowed owner; an empty setter returns ReadOnly, including when
initialized or assigned a typed null pointer.
It accepts named functions, `&function`, bare/+ captureless lambdas,
`Setter::bind<&function>()` and `Setter::bind<&Owner::method>(owner)`.
Method bindings have the same lvalue and lifetime requirements as Getter.
The getter/setter factory pair can also borrow two named capturing/stateful
callables with exact matching native value types.
`Setter::bindContext<&adapter>(owner)` accepts
`WriteResult(Owner&, const Scalar&) noexcept` and follows the same lifetime
contract. The public factories generate this adapter when needed.

Both public write methods are templates; ordinary callers use native values:

```cpp
auto result = index.write(makeId(0, 8), 250);   // int -> F32 VoltageLimit.
result = index.write(makeId(0, 8), 275.5);      // double -> F32.
result = field.write(12.7);                    // e.g. U16 receives 12.
```

The index uses the same direct lookup and positional bounds as reads.
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

WriteResult contains Applied, NotFound, ReadOnly, InvalidValue, Busy and
Unavailable (a slot-bound setter currently has no target).
TypeMismatch is unnecessary because writes convert supported numeric types.
The owner checks its semantic range and provides synchronization. Applied
means the value was applied before returning; it does not imply persistence
to Flash. Queued writes need a separate completion contract.

The callback's argument is borrowed for the duration of the call only. Copy
it if it must be retained. Calling a populated `field.set(...)` directly is
a low-level operation: use `field.write(...)` or `index.write(...)` to obtain
type conversion and numeric interval checks. Descriptor bindings and metadata
remain fixed; an explicitly used OwnerSlot or FunctionSlot may rebind under the lifetime and
synchronization contract above.

## Serialization

Use the existing borrowed index when exporting the same catalog repeatedly:

```cpp
writeSchema(index, schemaBuffer, sizeof(schemaBuffer));
writeValues(index, valuesBuffer, sizeof(valuesBuffer));
auto fingerprint = schemaCrc(index);

JsonOptions webSafe{JsonInt64Mode::String};
writeSchema(index, schemaBuffer, sizeof(schemaBuffer), webSafe);
writeValues(index, valuesBuffer, sizeof(valuesBuffer), webSafe);
```

The retained pointer/count overloads construct a CatalogIndex for that call.
All overloads publish exactly the capped group and field positions, matching
lookup. The schema includes each group's numeric `id` and each field's local
`i` plus packed `id`:

```json
{"id":1,"name":"sensor","fields":[{"i":0,"id":65536,"n":"Temperature","u":"degC","t":"f64","w":false,"f":0,"min":null,"max":null,"default":0}]}
```

Field schemas also publish this minimal root metadata, once per document:

```json
"meta":{"formatVersion":1,"fieldFlags":{"type":"u32","values":{"1":"Persistent"}}}
```

The dictionary keys are decimal **bit masks**, not bit positions; `f:0` means
none of the flags. Command schemas publish only `"meta":{"formatVersion":1}`.
There is no build date, slot state, ID layout or value-encoding profile in this
minimal header. `jsonSchemaFormatVersion` describes the JSON contract, separately
from ABI 7, any future storage format and the schema fingerprint. Old schemas
without `meta` predate this envelope. Adding known flag names need not change
the format version, but it changes the field fingerprint automatically.

Values retain named arrays such as `{"sensor":[24.5]}`. The order-sensitive
FNV-1a fingerprint includes group IDs, all four field-ID bytes, declared
metadata, setter presence (`w`), all four policy-mask bytes (`f`, little-endian),
min/max/default, enum codes/names/order when
present, and record/string boundaries. Limits are hashed by numeric value
bits with explicit byte order, never by object padding. A format marker
changes with compact native-bound encoding so previous cached schemas refresh.
The root format version and reflected field-flag codes/names are included too.
Command fingerprints include their format version without the field dictionary.
The ABI 7 schema adds `f` even for zero masks, so old cached field fingerprints
change. The fingerprint is a schema hint, not a Flash-format version or a promise
against collisions. The packed numbering and group schema IDs change the
previous playground schema; the production firmware is not changed.

Serializers use the same public traversal: indexed catalog/entry views where
IDs are exported or hashed, raw range iteration for values, and
`forEachParameter()` for command metadata. The [serializer comparison](../../tests/audit/SERIALIZATION_TRAVERSAL.md)
records unchanged JSON/fingerprints, buffer-boundary checks and ARM object/stack
differences from that implementation cleanup.
The later [versioned metadata addition](../../tests/audit/SCHEMA_META.md) changes
only the schema envelopes and fingerprints; values and field/command rows stay
the same.

Buffers belong to the caller; a zero returned length means failure and
partial JSON must not be sent. A null buffer fails regardless of its size;
a non-null buffer with positive size remains NUL-terminated. Serialization
stops at the first output failure, including further getter calls. Previously
read fields are not rolled back. The output must not overlap the metadata,
its strings or the source values. Names and units are non-null, NUL-terminated
UTF-8 strings. Quotes, backslashes and control bytes are escaped in both schema
and value-object keys, just as they are in enum labels. Names must remain
unique (catalog names globally, field names within each catalog).
Use `names_unique(fields, count)` for field tables and
`catalog_names_unique(catalogs, count)` for catalog tables. Both are optional
definition-time checks, outside lookup/read/write; for example:

```cpp
static_assert(catalog_names_unique(catalogs, std::size(catalogs)));
```

Both helpers reject null names (including a single row) and a null pointer
with a nonzero count. As with every pointer/count API, the count must describe
actual storage.

Null, failed normalization and non-finite floating-point values serialize as
JSON null. F32 and F64 use 9 and 17 significant digits respectively, preserving
round trips instead of shortening F32 to seven digits. JSON always uses a
decimal point, including under a decimal-comma locale; serialization does not
change the locale. The application must not concurrently call `setlocale`.
Floating formatting uses a bounded 64-byte temporary and the C library's
`snprintf`; newlib-nano builds need floating formatting enabled at link time
(for the verified CubeIDE configuration, `-Wl,-u,_printf_float`).
That temporary is not the total call-stack cost. The layered field serializer
at checkpoint `c6012d9` was measured on H7S at up to 1032/976 bytes for schema (`-O2`/`-Os`) and
968/888 bytes for values, including the nested newlib-nano calls, with the
output buffer outside the stack. Add any local output buffer, caller/getter frames and task/interrupt
headroom when budgeting integration. These observed cases are not a proven
worst-case bound and do not measure the new command serializer; see
[JSON stack measurements](../../tests/json_stack/README.md).

Every integer retains all decimal digits, including `UINT64_MAX` and
`INT64_MIN`; 8-bit integers serialize as numbers, not characters. The default
`JsonInt64Mode::Number` preserves the original wire format. In
`JsonInt64Mode::String`, only U64/S64 value elements and non-null U64/S64
schema bounds/defaults are quoted. U32/S32, floating values, booleans, nulls
and enum dictionary keys keep their existing representation. Native U64/S64
bounds compacted to null stay null. The choice is a wire representation only:
the logical schema fingerprint is identical in both modes.

U64/S64 use bounded decimal conversion with unsigned magnitude arithmetic, so
INT64_MIN does not overflow and newlib-nano's optional `long long` printf
support is not required. JavaScript Number cannot represent every U64/S64
integer outside `[-(2^53 - 1), 2^53 - 1]`; string mode preserves those digits
for such clients. Serialization does not provide a write transport,
subscriptions or scheduling.

## Verification and Cortex-M7 code generation

[TelemetryCheck.cpp](../../tests/TelemetryCheck.cpp) exercises the public
contracts, including count clipping, endpoints and actual 65536-component
capacity. [TelemetryWriteCheck.cpp](../../tests/TelemetryWriteCheck.cpp) checks
numeric boundaries, all 121 numeric/bool conversion pairs, native getter forms,
setter policy and write dispatch. [TelemetryReadCheck.cpp](../../tests/TelemetryReadCheck.cpp)
covers all inferred types, explicit reads, optional/Null handling, declared
type normalization, single getter invocation, variant access and floating
endpoints. It checks all 121 source/declared type pairs through both reads
and writes, and verifies that an explicit read type cannot bypass declared
rounding, truncation or range limits.
[TelemetryReadCompileFail.cpp](../../tests/TelemetryReadCompileFail.cpp) supplies
expected compilation failures, covering static reads, invalid
bindings, enum contracts and inconsistent limit definitions.
[TelemetryJsonCheck.cpp](../../tests/TelemetryJsonCheck.cpp) sweeps
buffer lengths, checks null output and early stopping, requires a decimal-comma
locale in CI, rejects null catalog/field/unit metadata safely, and checks 4096
samples plus endpoints for each of F32/F64/U64/S64. It also checks exact default
output, selective U64/S64 quoting, schema metadata and fingerprint stability in
string mode, and independence from source addresses and current values.
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
[TelemetryFactoryCheck.cpp](../../tests/TelemetryFactoryCheck.cpp) covers inferred
fields, native/enum setters, runtime owners, capture-free lambdas and stable
borrowed capturing/stateful callables, including the explicit Scalar escape
hatch.
[TelemetryCommandCheck.cpp](../../tests/TelemetryCommandCheck.cpp) covers command
conversion, side effects, schema, metadata lifetimes, indexed partial metadata,
owning tables, the two native dispatch levels and more than eight arguments.
[TelemetryFactoryCompileFail.cpp](../../tests/TelemetryFactoryCompileFail.cpp)
adds rejected definitions and calls, including temporary-table view
extractions plus compile-time typed position, arity and unsupported-type failures.
[TelemetryBorrowedFieldCompileFail.cpp](../../tests/TelemetryBorrowedFieldCompileFail.cpp)
checks temporary callable/owner rejection even with explicit const or reference
template arguments, along with accepted stable const bindings and inline lambdas.
The [test runner and instructions](../../tests/README.md) reproduce all suites,
standalone header compilation, rejected bindings and rejected fast-math flags.
[IndexCodegen.cpp](../../tests/IndexCodegen.cpp) is a compile-only ARM probe
with reproduction flags in its opening comment; use the same flags for
[ConversionCodegen.cpp](../../tests/ConversionCodegen.cpp),
[DeclaredTypeCodegen.cpp](../../tests/DeclaredTypeCodegen.cpp),
[ScalarStorageCodegen.cpp](../../tests/ScalarStorageCodegen.cpp),
[ScalarVisitCodegen.cpp](../../tests/ScalarVisitCodegen.cpp),
[EnumCodegen.cpp](../../tests/EnumCodegen.cpp) and
[LimitsCodegen.cpp](../../tests/LimitsCodegen.cpp),
[FactoryCodegen.cpp](../../tests/FactoryCodegen.cpp),
[BorrowedFieldCodegen.cpp](../../tests/BorrowedFieldCodegen.cpp) and
[CommandTableCodegen.cpp](../../tests/CommandTableCodegen.cpp).
The [ARM runner](../../tests/run_arm_checks.py) compiles all fifteen probes and
all positive suites at `-O2`/`-Os`, checks for startup initialization/writable
probe storage, pins exported table sizes, links the newlib-nano consumer and
independently checks core, field-JSON and command-JSON archives. Each matching layout links; each
deliberately mixed 32/64-byte layout must fail.
GitHub Actions runs it with Ubuntu's ARM GCC; the same runner also passes
with the local CubeIDE compiler. Disassembly is retained, and the runner rejects
typed wrappers that regain stack storage, Scalar references or an indirect
branch. It also requires a direct relocation to the concrete command target and
keeps the erased wrapper as an explicit indirect-dispatch control.
It compares manual and inferred read wrappers in the same build and rejects
growth. The [factory checkpoint](../../tests/README.md#signature-factory-and-command-codegen)
records a separate 14/14 object-byte comparison against `c6012d9` and the free
getter improvement. The current ABI-7 layout is Field 96, Command 20 and Catalog 12 bytes on ARM32.
See [positional table checks](../../tests/position_tables/README.md) for current evidence.

### Historical code-generation and board checkpoints

The following counts and comparisons refer to the stated earlier revisions;
they are not a substitute for running the current source checks.

For this final core update, local CubeIDE GCC 14.3.1 compiled 27 translation
units and ten probes at both optimization levels. All 18 `-O2`/`-Os` objects
from the nine pre-existing probes match checkpoint `55fbd481` byte for byte.
The borrowed-field probe exports a 96-byte, 32-aligned Field in `.rodata`; the
owning-table probe keeps its internal table and exported view/count read-only,
with no nonzero `.data` or `.bss`.

The owning-table probe also compiles all three execution levels with runtime
values. At both `-O2` and `-Os`, CubeIDE GCC 14.3.1 emits 48 bytes for
`call<1>()`, 60 bytes for runtime-position native dispatch and 52 bytes for
`execute()` with a prebuilt Scalar array. Both native wrappers use no stack and
tail-branch directly to `CommandProbeDevice::configure`. The descriptor path
uses a four-byte register spill and an indirect tail branch. Wrapper byte count
alone is not a timing comparison; the live DWT fixture measures execution
separately.

That [retained H7S measurement](../../tests/command_dispatch/h7s/README.md)
records 28.001 / 29.001 / 88.001 cycles per call at `-O2` for compile-time
index, runtime native index and runtime ID with prebuilt Scalars respectively.
At `-Os` the same paths take 24.001 / 27.001 / 92.001 cycles. Each result is the
median of nine 65,536-call windows; all checksums passed and the original image
was restored byte for byte.

For the layered refactor, the local CubeIDE GCC 14.3.1 runner compiled 20
translation units at both optimization levels. Its seven hot-path probes were
also built from checkpoint `036d8e8` with the same command. Every resulting
`-O2` and `-Os` object matched byte for byte (**14/14**), including relocations
and constants. This covers conversion, declared-type reads/writes, enum and
limit data paths, direct indexing, Scalar storage and Scalar visitation. The
JSON object is intentionally different because the serializer now supports the
selectable U64/S64 string representation; its final stack behavior was measured
separately on the board.

CubeIDE GCC 14.3.1, C++17, Cortex-M7, `-O2` and `-Os` produce direct lookups
with no loops, helper calls or allocations. Successful lookup through a
passed view takes 14 instructions in the measured object; a fixed constexpr
view takes 13. A known ID becomes a constant address (`ldr; bx`), and a known
missing ID becomes null. These are instruction counts, not measured cycles.

On Cortex-M7, Scalar occupies 16 bytes, Getter 8, Setter 8, FieldType 48, Field 96,
Catalog 16 and CatalogIndex 8 bytes. Getter/Setter each remain one target-sized
payload plus one invoker; an empty setter still occupies its 8-byte slot.
FieldType owns native bounds, a default Scalar and an optional schema callback.
The probe's constant metadata resides in `.rodata`,
with no startup constructor sections and zero `.data`/`.bss`. Mutable source
values are external to the probe and still need application storage. Final
Flash/RAM placement is determined by linking.
For 1000 fields the Field array alone occupies 96,000 bytes (93.75 KiB),
before strings, callback code or catalogs. This is the descriptor cost of owning
limits/defaults in every descriptor. Sharing separate schema descriptors is
a possible future layout change, not an optimization applied by this release.

The final [exact compact-callback A/B](../../tests/field_layout/h7s/COMPACT_CALLBACK_RESULTS.md)
compares checkpoint `a452283` with the two-word callback core on a 600 MHz H7S3.
For shuffled access to 1024 RAM fields at `-O2`, Scalar read improved 5.07%,
float read 8.21% and U16 write 13.35%; the linked image shrank by 1696 bytes.
At `-Os`, reads improved and float write was unchanged; U16 write moved by
+0.77%. Complete normalized disassembly of the `-Os` F32/U16 write wrappers is
instruction-identical, so that sub-cycle remainder is recorded as link-placement
sensitivity rather than an added operation. All 1840 timing windows, result
checksums, image/object hashes and byte-exact firmware restoration pass the
offline evidence verifier. These are H7S fixture measurements, not H753
firmware-wide timing.

The enum/plain U16 pairs in `EnumCodegen.cpp` use the same bounds and numeric
operations at both optimization levels, apart from table addresses/offsets
and the corresponding instruction encodings.
Known typed reads branch straight to their shared getter. Dynamic Field
reads access the numeric tag at offset 8; writes use the descriptor tag at
offset 40. They never load the schema callback at offset 80. Reads never load limits.
The enum probe's constant
names and field arrays reside in `.rodata`, with no startup constructors or
`.data`/`.bss` storage. These observations do not assert identical cache
behavior after increasing the size of a Field.

`LimitsCodegen.cpp` confirms that bounded F32/U16 reads still branch directly
to getters. ScalarType, zero-argument numericType and default-only numericType
have the same numeric write operations for U16/F32, apart from addresses and
branch encodings. The default U16 write has no interval comparison; a custom U16
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
read-only and missing writes reduce to constant result returns. Bound callback
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
