// Mutable function selection must preserve native calls and immutable schemas.
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
FunctionSlot<float() noexcept> reader;
FunctionSlot<WriteResult(float) noexcept> writer;
FunctionSlot<Mode() noexcept> modeReader;
FunctionSlot<WriteResult(Mode) noexcept> modeWriter;
FunctionSlot<Scalar() noexcept> scalarReader;
FunctionSlot<WriteResult(const Scalar&) noexcept> scalarWriter;
FunctionSlot<CommandResult(float, Mode) noexcept> configure;
FunctionSlot<CommandResult() noexcept> reset;
constexpr FieldTable rows{
    field("Voltage", "V", reader, writer, limits(230.f, 0.f, 500.f)),
    field("Read only", "V", reader),
    field("Mode", "", modeReader, modeWriter),
    field("Scalar", "V", ScalarType::F32, scalarReader, scalarWriter)};
constexpr FieldCatalogTable fields{group("meter", rows)};
constexpr CommandTable commands{
    command("Configure", configure, arg<0>("Voltage", "V", 230.f, 0.f, 500.f)),
    command("Reset", reset)};
constexpr CommandCatalogTable actions{group("meter", commands)};
static_assert(sizeof(reader) == sizeof(decltype(reader)::Function));
static_assert(!std::is_copy_constructible_v<decltype(reader)> && !std::is_move_constructible_v<decltype(reader)>);
static_assert(!std::is_invocable_v<decltype(reader)&>, "no unchecked call operator");
static_assert(sizeof(rows) == 4 * sizeof(Field) && sizeof(Command) == 5 * sizeof(void*));
static_assert(telemetryAbiVersion == 7);
static_assert(std::is_same_v<decltype(rows.read<0>()), std::optional<float>>);
static_assert(std::is_same_v<decltype(rows.read<2>()), std::optional<std::uint16_t>>);
static_assert(std::is_same_v<decltype(rows.read<3>()), Scalar>);

float first = 230, second = 410;
int reads = 0, writes = 0, calls = 0;
Mode mode = Mode::Auto;
float readA() noexcept { ++reads; return first; }
float readB() noexcept { ++reads; return second; }
WriteResult writeA(float value) noexcept { ++writes; first = value; return WriteResult::Applied; }
WriteResult writeB(float value) noexcept { ++writes; second = value; return WriteResult::Busy; }
CommandResult callA(float value, Mode next) noexcept
{ ++calls; first = value; mode = next; return CommandResult::Executed; }
CommandResult callB(float value, Mode next) noexcept
{ ++calls; second = value; mode = next; return CommandResult::Accepted; }
constexpr bool slotConstantOperations() noexcept
{
    FunctionSlot<float() noexcept> value;
    if (value || value.get() != nullptr) return false;
    value.bind(&readA);
    if (!value || value.get() != &readA) return false;
    value.reset();
    value.bind(nullptr);
    return !value;
}
static_assert(slotConstantOperations());

void unavailable()
{
    expect(!reader && !writer && !configure, "empty/reset slot state");
    expect(!rows.read<0>() && !rows.read<0, double>() && !fields.read<0>(), "native reads preserve absence");
    expect(fields.read(0).type() == ScalarType::Null && !fields.read<float>(0)
           && rows[0].get().type() == ScalarType::Null, "erased and raw getter reads preserve absence");
    expect(!rows.read<2>() && rows.read<3>().type() == ScalarType::Null && !rows.read<3, float>(),
           "enum and explicit Scalar getters preserve absence");
    expect(rows.write<0>(250) == WriteResult::Unavailable && fields.write<0>(250.) == WriteResult::Unavailable
           && fields.write(0, 250) == WriteResult::Unavailable
           && rows[0].set(Scalar::fromF32(250)) == WriteResult::Unavailable, "empty setter is unavailable");
    expect(rows.write<0>(501) == WriteResult::InvalidValue && fields.write(0, 501) == WriteResult::InvalidValue
           && rows.write<0>(std::numeric_limits<double>::infinity()) == WriteResult::InvalidValue,
           "write conversion and limits precede setter availability on both routes");
    expect(rows.write<1>(250) == WriteResult::ReadOnly && fields.write(1, Scalar{}) == WriteResult::ReadOnly,
           "absent setter capability remains read-only");
    expect(rows.write<3>(250) == WriteResult::Unavailable && fields.write(3, 250) == WriteResult::Unavailable,
           "explicit Scalar setter checks function snapshot");
    expect(commands.call<0>(250., 1) == CommandResult::Unavailable
           && commands.call(std::size_t{0}, 250, 1) == CommandResult::Unavailable
           && actions.call<0>(250., 1) == CommandResult::Unavailable
           && actions.index().call(0, 250, 1) == CommandResult::Unavailable
           && commands.call<1>() == CommandResult::Unavailable, "all command routes preserve absence");
    expect(commands.call<0>(std::numeric_limits<double>::infinity(), 99) == CommandResult::Unavailable
           && actions.index().call(0, std::numeric_limits<double>::infinity(), 99) == CommandResult::Unavailable,
           "command target availability precedes numeric conversion");
    expect(actions.execute(0, nullptr, 0) == CommandResult::ArgumentCountMismatch
           && actions.execute(0, nullptr, 2) == CommandResult::InvalidValue,
           "transport shape checks precede target availability");
}
} // namespace

int main()
{
    unavailable();
    char fieldSchema[4096], commandSchema[4096], current[4096];
    const auto fieldLength = writeSchema(fields, fieldSchema, sizeof fieldSchema);
    const auto commandLength = writeSchema(actions, commandSchema, sizeof commandSchema);
    expect(fieldLength != 0 && commandLength != 0 && rows[0].get && rows[0].set,
           "complete schema and capabilities exist before binding");
    reader.bind(readA);
    writer.bind(&writeA);
    configure.bind(&callA);
    modeReader.bind([]() noexcept { return mode; });
    modeWriter.bind(+[](Mode value) noexcept { mode = value; return WriteResult::Applied; });
    scalarReader.bind([]() noexcept { return Scalar::fromF64(first + 0.75); });
    scalarWriter.bind([](const Scalar& value) noexcept { first = value.get<float>(); return WriteResult::Applied; });
    reset.bind([]() noexcept { ++calls; return CommandResult::Busy; });
    expect(reader && reader.get() == &readA && configure.get() == &callA, "exact function pointers retained");
    expect(rows.read<0>() == 230.f && rows.read<0, double>() == 230.
           && fields.read<0>() == 230.f && fields.read<float>(0) == 230.f && reads == 4,
           "all read routes invoke the current function exactly once");
    expect(rows.read<2>() == 1 && rows.read<3>().get<float>() == 230.75f,
           "enum and explicit Scalar normalization preserved");
    expect(rows.write<0>(250.5) == WriteResult::Applied && first == 250.5f
           && fields.write<0>(260) == WriteResult::Applied && first == 260.f
           && fields.write(0, 270) == WriteResult::Applied && first == 270.f && writes == 3,
           "native and erased writes convert to the exact setter signature");
    expect(rows[0].set(Scalar::fromU16(25)) == WriteResult::InvalidValue && writes == 3,
           "raw setter never extracts a wrong Scalar alternative");
    expect(rows.write<0>(501) == WriteResult::InvalidValue && fields.write(0, -1) == WriteResult::InvalidValue
           && writes == 3, "invalid values never invoke the setter");
    expect(rows.write<2>(2) == WriteResult::Applied && mode == Mode::Manual
           && fields.write(2, 3) == WriteResult::InvalidValue, "enum bounds and native conversion preserved");
    expect(rows.write<3>(275) == WriteResult::Applied && first == 275.f,
           "explicit Scalar callback receives normalized F32");
    expect(commands.call<0>(280., 1) == CommandResult::Executed && first == 280.f && mode == Mode::Auto
           && commands.call(std::size_t{0}, 285, 2) == CommandResult::Executed
           && actions.call<0>(290., 0) == CommandResult::Executed && first == 290.f && mode == Mode::Off,
           "typed local/runtime/global commands convert without Scalar transport");
    const Scalar args[]{Scalar::fromU16(300), Scalar::fromU8(1)};
    expect(actions.execute(0, args, 2) == CommandResult::Executed && first == 300.f && mode == Mode::Auto,
           "transport uses the same current function and limits");
    expect(commands.call<0>(501, 0) == CommandResult::InvalidValue
           && actions.index().call(0, 250, 3) == CommandResult::InvalidValue && calls == 4,
           "invalid commands have no side effects");
    expect(commands.call<1>() == CommandResult::Busy && actions.execute(1, nullptr, 0) == CommandResult::Busy,
           "zero-argument slot preserves callback result");
    reader.bind(&readB); writer.bind(&writeB); configure.bind(&callB);
    expect(rows.read<0>() == 410.f && fields.read<float>(0) == 410.f, "getter rebind is visible immediately");
    expect(rows.write<0>(420) == WriteResult::Busy && second == 420.f && first == 300.f,
           "setter rebind preserves result and no longer touches old state");
    expect(actions.call<0>(430, 2) == CommandResult::Accepted && second == 430.f && first == 300.f,
           "command rebind preserves result and no longer touches old state");
    expect(writeSchema(fields, current, sizeof current) == fieldLength && std::strcmp(current, fieldSchema) == 0,
           "field schema and fingerprint unchanged by rebind");
    expect(writeSchema(actions, current, sizeof current) == commandLength && std::strcmp(current, commandSchema) == 0,
           "command schema and fingerprint unchanged by rebind");
    reader.bind([]() noexcept { reader.bind(&readB); return 19.f; });
    expect(rows.read<0>() == 19.f && rows.read<0>() == 430.f, "one function snapshot per call, including self-rebind");
    const auto& constReader = reader;
    const auto& constWriter = writer;
    const auto& constCommand = configure;
    FieldTable constFields{field("Const view", "", constReader, constWriter)};
    CommandTable constCommands{command("Const view", constCommand)};
    expect(constFields.read<0>() == 430.f && constFields.write<0>(440) == WriteResult::Busy
           && constCommands.call<0>(450, 1) == CommandResult::Accepted, "const slot views invoke latest target");
    reader.reset();
    expect(!rows.read<0>() && rows.write<0>(460) == WriteResult::Busy && second == 460.f,
           "getter and setter slots are independently available");
    writer.bind(nullptr);
    configure.reset(); modeReader.reset(); modeWriter.reset(); scalarReader.reset(); scalarWriter.reset(); reset.reset();
    unavailable();
    struct ReadFunctor { float value = 17; float operator()() noexcept { return value; } } readFunctor;
    struct WriteFunctor { float value = 0; WriteResult operator()(float v) noexcept { value = v; return WriteResult::Applied; } } writeFunctor;
    FieldTable mixed{field("Normal read", "", readFunctor, writer), field("Normal write", "", reader, writeFunctor)};
    expect(mixed.read<0>() == 17.f && mixed.write<0>(3) == WriteResult::Unavailable
           && !mixed.read<1>() && mixed.write<1>(7) == WriteResult::Applied && writeFunctor.value == 7,
           "slot and ordinary borrowed callable can be paired without coupling availability");
    FunctionSlot<std::uint64_t() noexcept> wide;
    wide.bind([]() noexcept { return std::numeric_limits<std::uint64_t>::max(); });
    FieldTable wideFields{field("Wide", "", wide)};
    expect(wideFields.read<0>() == std::numeric_limits<std::uint64_t>::max(), "wide integer getter remains exact");
    std::printf("%d/%d function slot checks passed\n", checks - failures, checks);
    return failures ? 1 : 0;
}
