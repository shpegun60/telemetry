// Late binding must be safe before bind(), after reset(), and across rebinds.
// Authors: Ruslan Kovtun (shpegun60), codexAi. License: MIT.
#include "Telemetry.h"
#include "serialization/TelemetryCommandJson.h"
#include <cstdio>
#include <cstring>
#include <limits>

using namespace telemetry;
namespace {
int checks = 0, failures = 0;
void expect(bool ok, const char* label)
{ ++checks; if (!ok) { ++failures; std::printf("FAIL %s\n", label); } }
enum class Mode : std::uint16_t { Off, Auto, Manual };
struct Meter {
    float voltage = 230;
    Mode mode = Mode::Auto;
    int reads = 0, writes = 0, calls = 0;
    float read() noexcept { ++reads; return voltage; }
    float readConst() const noexcept { return voltage; }
    WriteResult write(float value) noexcept { ++writes; voltage = value; return WriteResult::Applied; }
    Mode readMode() const noexcept { return mode; }
    WriteResult writeMode(Mode value) noexcept { ++writes; mode = value; return WriteResult::Busy; }
    Scalar scalarRead() noexcept { ++reads; return Scalar::fromF64(voltage + 0.75); }
    WriteResult scalarWrite(const Scalar& value) noexcept
    { ++writes; voltage = value.get<float>(); return WriteResult::Applied; }
    CommandResult configure(float value, Mode next) noexcept
    { ++calls; voltage = value; mode = next; return CommandResult::Accepted; }
    CommandResult reset() noexcept { ++calls; voltage = 0; return CommandResult::Executed; }
    // bind() must use addressof rather than this overload.
    Meter* operator&() noexcept { return nullptr; }
};
OwnerSlot<Meter> owner;
constexpr FieldTable rows{
    field<&Meter::read, &Meter::write>("Voltage", "V", owner, limits(230.f, 0.f, 500.f)),
    field<&Meter::readConst>("Read only", "V", owner),
    field<&Meter::readMode, &Meter::writeMode>("Mode", "", owner),
    field<&Meter::scalarRead, &Meter::scalarWrite>("Scalar", "V", ScalarType::F32, owner)};
constexpr FieldCatalogTable fields{group("meter", rows)};
constexpr CommandTable commands{
    command<&Meter::configure>("Configure", owner,
        arg<0>("Voltage", "V", 230.f, 0.f, 500.f)),
    command<&Meter::reset>("Reset", owner)};
constexpr CommandCatalogTable actions{group("meter", commands)};
static_assert(sizeof(OwnerSlot<Meter>) == sizeof(Meter*) && alignof(OwnerSlot<Meter>) == alignof(Meter*));
static_assert(!std::is_copy_constructible_v<OwnerSlot<Meter>> && !std::is_move_constructible_v<OwnerSlot<Meter>>);
static_assert(sizeof(rows) == 4 * sizeof(Field) && sizeof(Command) == 5 * sizeof(void*));
static_assert(telemetryAbiVersion == 7 && static_cast<int>(WriteResult::Busy) == 4);
static_assert(std::is_same_v<decltype(rows.read<0>()), std::optional<float>>);
static_assert(std::is_same_v<decltype(rows.read<2>()), std::optional<std::uint16_t>>);
static_assert(std::is_same_v<decltype(rows.read<3>()), Scalar>);

float freeValue = 17;
float freeRead() noexcept { return freeValue; }
WriteResult freeWrite(float value) noexcept { freeValue = value; return WriteResult::Applied; }
constexpr FieldTable mixed{
    field<&freeRead, &Meter::write>("Free read", "", owner),
    field<&Meter::read, &freeWrite>("Free write", "", owner)};

void unavailable()
{
    expect(!owner && owner.get() == nullptr, "default/reset slot is empty");
    expect(!rows.read<0>() && !rows.read<0, double>() && !fields.read<makeId(0, 0)>(),
           "native reads return empty optional before invoking an absent owner");
    expect(fields.read(0).type() == ScalarType::Null && !fields.read<float>(0)
           && rows[0].get().type() == ScalarType::Null,
           "erased and raw getter reads return Null");
    expect(!rows.read<2>() && rows.read<3>().type() == ScalarType::Null && !rows.read<3, double>(),
           "enum and explicit Scalar getters are safe with empty slots");
    expect(rows.write<0>(250) == WriteResult::Unavailable
           && fields.write<0>(250.0) == WriteResult::Unavailable
           && fields.write(0, 250.0) == WriteResult::Unavailable
           && rows[0].set(Scalar::fromF32(250)) == WriteResult::Unavailable,
           "valid writes report an empty slot on every API");
    expect(rows.write<0>(501) == WriteResult::InvalidValue
           && fields.write(0, 501) == WriteResult::InvalidValue
           && rows.write<0>(std::numeric_limits<double>::infinity()) == WriteResult::InvalidValue
           && fields.write(0, std::numeric_limits<double>::infinity()) == WriteResult::InvalidValue,
           "field normalization and limits precede availability on both write routes");
    expect(rows.write<1>(250) == WriteResult::ReadOnly && fields.write(1, Scalar{}) == WriteResult::ReadOnly,
           "read-only status remains independent of slot state and supplied value");
    expect(rows.write<3>(250) == WriteResult::Unavailable
           && fields.write(3, 250) == WriteResult::Unavailable,
           "explicit Scalar setters also check their slot");
    expect(commands.call<0>(250.0, 1) == CommandResult::Unavailable
           && commands.call(std::size_t{0}, 250, 1) == CommandResult::Unavailable
           && actions.call<0>(250.0, 1) == CommandResult::Unavailable
           && actions.index().call(0, 250, 1) == CommandResult::Unavailable
           && commands.call<1>() == CommandResult::Unavailable,
           "all command routes refuse to invoke an absent owner");
    expect(commands.call<0>(std::numeric_limits<double>::infinity(), 99) == CommandResult::Unavailable
           && actions.index().call(0, std::numeric_limits<double>::infinity(), 99) == CommandResult::Unavailable,
           "commands resolve once before numeric conversion");
    expect(actions.execute(0, nullptr, 0) == CommandResult::ArgumentCountMismatch
           && actions.execute(0, nullptr, 2) == CommandResult::InvalidValue,
           "transport array shape checks still precede command dispatch");
}
} // namespace

int main()
{
    unavailable();
    char fieldSchema[4096], commandSchema[4096], current[4096];
    const auto fieldLength = writeSchema(fields, fieldSchema, sizeof fieldSchema);
    const auto commandLength = writeSchema(actions, commandSchema, sizeof commandSchema);
    expect(fieldLength != 0 && commandLength != 0 && rows[0].get && rows[0].set,
           "unbound tables already export complete schema and callback capability");
    Meter first, second;
    second.voltage = 400;
    owner.bind(first);
    expect(owner.get() == std::addressof(first) && owner,
           "bind stores the true address even with overloaded operator address");
    expect(rows.read<0>() == 230.f && rows.read<0, double>() == 230.0
           && fields.read<0>() == 230.f && fields.read<float>(0) == 230.f && first.reads == 4,
           "native local/global and erased reads invoke the bound owner exactly once");
    expect(rows.read<2>() == 1 && rows.read<3>().get<float>() == 230.75f,
           "bound enum and Scalar getters retain their original normalization");
    expect(rows.write<0>(250.5) == WriteResult::Applied && first.voltage == 250.5f
           && fields.write<0>(260) == WriteResult::Applied && first.voltage == 260.f
           && fields.write(0, 270) == WriteResult::Applied && first.voltage == 270.f,
           "native and erased writes keep checked conversion and custom bounds");
    expect(rows.write<2>(2) == WriteResult::Busy && first.mode == Mode::Manual
           && fields.write(2, 3) == WriteResult::InvalidValue,
           "slot enum setters validate codes and preserve callback results");
    expect(rows.write<3>(280) == WriteResult::Applied && first.voltage == 280.f,
           "explicit Scalar write normalizes before the bound setter");
    expect(commands.call<0>(310.25, 1) == CommandResult::Accepted
           && commands.call(std::size_t{0}, 320, 2) == CommandResult::Accepted
           && actions.call<0>(330.5, 0) == CommandResult::Accepted
           && actions.index().call(0, 340, 1) == CommandResult::Accepted
           && first.calls == 4 && first.voltage == 340.f && first.mode == Mode::Auto,
           "slot commands retain native conversions and shared validation");
    expect(commands.call<0>(501, 0) == CommandResult::InvalidValue
           && actions.index().call(0, 250, 99) == CommandResult::InvalidValue && first.calls == 4,
           "failed arguments never reach the bound command");
    const int oldReads = first.reads, oldWrites = first.writes, oldCalls = first.calls;
    owner.bind(second);
    expect(rows.read<0>() == 400.f && fields.read<float>(0) == 400.f
           && rows.write<0>(410) == WriteResult::Applied
           && actions.call<0>(420, 2) == CommandResult::Accepted
           && second.voltage == 420.f && second.mode == Mode::Manual
           && first.reads == oldReads && first.writes == oldWrites && first.calls == oldCalls,
           "rebind redirects all existing const tables without touching the old object");
    expect(writeSchema(fields, current, sizeof current) == fieldLength
           && std::strcmp(current, fieldSchema) == 0
           && writeSchema(actions, current, sizeof current) == commandLength
           && std::strcmp(current, commandSchema) == 0,
           "binding and owner state never change schema or its fingerprint");
    owner.reset();
    unavailable();

    expect(mixed.read<0>() == 17.f && mixed[0].read<float>() == 17.f,
           "a free getter ignores a slot used only by its member setter");
    expect(mixed.write<1>(18) == WriteResult::Applied && freeValue == 18.f
           && mixed[1].write(19) == WriteResult::Applied && freeValue == 19.f,
           "a free setter ignores a slot used only by its member getter");
    const Meter immutable;
    OwnerSlot<const Meter> constOwner;
    constOwner.bind(immutable);
    const FieldTable constRows{field<&Meter::readConst>("Const", "", constOwner)};
    expect(constRows.read<0>() == 230.f && constRows[0].read<float>() == 230.f,
           "const owners retain const member access");
    const auto& slotView = owner;
    owner.bind(first);
    const FieldTable viewRows{field<&Meter::read>("View", "", slotView)};
    expect(viewRows.read<0>() == first.voltage, "const slot view does not change its target qualification");

    struct Prefix { std::uint64_t marker = UINT64_C(0x12345678); };
    struct Base {
        virtual float read() const noexcept { return -1; }
        virtual WriteResult write(float) noexcept { return WriteResult::Busy; }
        virtual CommandResult call(float) noexcept { return CommandResult::Failed; }
        virtual ~Base() = default;
    };
    struct Derived : Prefix, Base {
        float value = 42;
        float read() const noexcept override { return value; }
        WriteResult write(float v) noexcept override { value = v; return WriteResult::Applied; }
        CommandResult call(float v) noexcept override { value = v; return CommandResult::Executed; }
    } derived;
    OwnerSlot<Base> baseSlot;
    OwnerSlot<Derived> derivedSlot;
    baseSlot.bind(derived); derivedSlot.bind(derived);
    const FieldTable inherited{
        field<&Base::read, &Base::write>("Base", "", baseSlot),
        field<&Base::read, &Base::write>("Derived", "", derivedSlot)};
    const CommandTable inheritedCommands{command<&Base::call>("Call", derivedSlot)};
    expect(inherited.read<0>() == 42 && inherited.read<1>() == 42
           && inherited.write<0>(43) == WriteResult::Applied
           && inherited[1].write(44) == WriteResult::Applied
           && inheritedCommands.call<0>(45.0) == CommandResult::Executed
           && derived.value == 45 && derived.marker == UINT64_C(0x12345678),
           "base adjustment and virtual dispatch survive both slot target forms");
    owner.reset();
    std::printf("%d/%d owner slot checks passed\n", checks - failures, checks);
    return failures != 0;
}
