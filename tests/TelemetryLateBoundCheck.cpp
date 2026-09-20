// Context, borrowed and owned callbacks share field/command absence contracts.
// Authors: Ruslan Kovtun (shpegun60), codexAi. License: MIT.
#include "Telemetry.h"
#include "serialization/TelemetryCommandJson.h"
#include <cstdio>
#include <cstring>
#include <memory>
using namespace telemetry;
namespace {
int checks = 0, failures = 0;
void expect(bool ok, const char* label)
{ ++checks; if (!ok) { ++failures; std::printf("FAIL %s\n", label); } }
enum class Mode : std::uint16_t { Off, Auto, Manual };
struct Meter {
    float value = 23.f;
    Mode mode = Mode::Off;
    int reads = 0, writes = 0, calls = 0;
    float read() noexcept { ++reads; return value; }
    float readConst() const noexcept { return value; }
    WriteResult write(float v) noexcept { ++writes; value = v; return WriteResult::Applied; }
    CommandResult call(float v, Mode m) noexcept { ++calls; value = v; mode = m; return CommandResult::Accepted; }
};
ContextFunctionSlot<float() noexcept> contextRead;
ContextFunctionSlot<WriteResult(float) noexcept> contextWrite;
ContextFunctionSlot<CommandResult(float, Mode) noexcept> contextCall;
DelegateRefSlot<float() noexcept> borrowedRead;
DelegateRefSlot<WriteResult(float) noexcept> borrowedWrite;
DelegateRefSlot<CommandResult(float, Mode) noexcept> borrowedCall;
DelegateSlot<float() noexcept, 32> ownedRead;
DelegateSlot<WriteResult(float) noexcept, 32> ownedWrite;
DelegateSlot<CommandResult(float, Mode) noexcept, 32> ownedCall;
constexpr FieldTable rows{
    field("Context", "V", contextRead, contextWrite, limits(23.f, 0.f, 500.f)),
    field("Borrowed", "V", borrowedRead, borrowedWrite, limits(23.f, 0.f, 500.f)),
    field("Owned", "V", ownedRead, ownedWrite, limits(23.f, 0.f, 500.f))};
constexpr FieldCatalogTable fields{group("slots", rows)};
constexpr CommandTable commands{
    command("Context", contextCall, arg<0>("Value", "V", 23.f, 0.f, 500.f)),
    command("Borrowed", borrowedCall, arg<0>("Value", "V", 23.f, 0.f, 500.f)),
    command("Owned", ownedCall, arg<0>("Value", "V", 23.f, 0.f, 500.f))};
constexpr CommandCatalogTable actions{group("slots", commands)};
static_assert(sizeof(contextRead) == 2 * sizeof(void*));
static_assert(sizeof(borrowedRead) == 2 * sizeof(void*));
static_assert(sizeof(ownedRead) == sizeof(tiny::delegate<float(), 32>));
static_assert(sizeof(rows) == 3 * sizeof(Field) && telemetryAbiVersion == 6);
static_assert(!std::is_copy_constructible_v<decltype(contextRead)> && !std::is_move_constructible_v<decltype(contextRead)>);
static_assert(!std::is_copy_constructible_v<decltype(borrowedRead)> && !std::is_move_constructible_v<decltype(borrowedRead)>);
static_assert(!std::is_copy_constructible_v<decltype(ownedRead)> && !std::is_move_constructible_v<decltype(ownedRead)>);
static_assert(!std::is_invocable_v<decltype(ownedRead)&> && !std::is_invocable_v<decltype(contextRead)&>);
static_assert(!std::is_default_constructible_v<decltype(borrowedRead.get())>
              && !std::is_default_constructible_v<decltype(ownedRead.get())>, "slot views always refer to a real slot");
float readContext(void* context) noexcept { return static_cast<Meter*>(context)->read(); }
WriteResult writeContext(void* context, float v) noexcept { return static_cast<Meter*>(context)->write(v); }
CommandResult callContext(void* context, float v, Mode m) noexcept { return static_cast<Meter*>(context)->call(v, m); }
float readConstant() noexcept { return 17.f; }

template <std::size_t I> void absent()
{
    expect(!rows.read<I>() && !rows.read<I, double>() && !fields.read<makeId(0, I)>(), "native getter absence");
    expect(fields.read(makeId(0, I)).type() == ScalarType::Null && !fields.read<float>(makeId(0, I)), "transport getter absence");
    expect(rows.write<I>(50) == WriteResult::Unavailable && fields.write(makeId(0, I), 50) == WriteResult::Unavailable,
           "empty setter is unavailable on both routes");
    expect(rows.write<I>(501) == WriteResult::InvalidValue && fields.write(makeId(0, I), -1) == WriteResult::InvalidValue,
           "write normalization precedes target availability");
    expect(commands.call<I>(250., 1) == CommandResult::Unavailable
           && commands.call(I, 250., 1) == CommandResult::Unavailable
           && actions.index().call(makeId(0, I), 250., 1) == CommandResult::Unavailable, "command absence on all routes");
    expect(actions.execute(makeId(0, I), nullptr, 0) == CommandResult::ArgumentCountMismatch
           && actions.execute(makeId(0, I), nullptr, 2) == CommandResult::InvalidValue, "transport shape checked first");
}
template <std::size_t I> void roundTrip(Meter& meter)
{
    const int reads = meter.reads;
    expect(rows.read<I>() == meter.value && fields.read<float>(makeId(0, I)) == meter.value && meter.reads == reads + 2,
           "bound getters invoke once on native and transport routes");
    expect(rows.write<I>(110.5) == WriteResult::Applied && meter.value == 110.5f
           && fields.write(makeId(0, I), 115) == WriteResult::Applied && meter.value == 115.f, "native setter conversion");
    const int writes = meter.writes;
    expect(rows[I].set(Scalar::fromU16(123)) == WriteResult::InvalidValue
           && rows.write<I>(501) == WriteResult::InvalidValue && meter.writes == writes, "invalid write cannot invoke");
    expect(commands.call<I>(120., 1) == CommandResult::Accepted && meter.value == 120.f && meter.mode == Mode::Auto
           && actions.call<makeId(0, I)>(125, 2) == CommandResult::Accepted && meter.mode == Mode::Manual,
           "native slot commands keep checked numeric/enum conversion");
    const Scalar args[]{130., std::uint8_t{0}};
    expect(actions.execute(makeId(0, I), args, 2) == CommandResult::Accepted && meter.value == 130.f && meter.mode == Mode::Off,
           "transport command conversion");
    const int calls = meter.calls;
    expect(commands.call<I>(501, 0) == CommandResult::InvalidValue
           && actions.index().call(makeId(0, I), 130, 3) == CommandResult::InvalidValue && meter.calls == calls,
           "invalid command cannot invoke");
}
struct Tracked {
    int* live;
    float value;
    Tracked(int& count, float v) noexcept : live(&count), value(v) { ++*live; }
    Tracked(const Tracked& other) noexcept : live(other.live), value(other.value) { ++*live; }
    Tracked(Tracked&& other) noexcept : live(other.live), value(other.value) { ++*live; }
    ~Tracked() noexcept { --*live; }
    float operator()() noexcept { return value; }
};
struct Self {
    int value = 123;
    Self& operator()() noexcept { return *this; }
};
struct ResetOnDestroy {
    decltype(ownedRead)* slot;
    explicit ResetOnDestroy(decltype(ownedRead)& target) noexcept : slot(&target) {}
    ResetOnDestroy(ResetOnDestroy&& other) noexcept : slot(other.slot) { other.slot = nullptr; }
    ~ResetOnDestroy() noexcept { if (slot) slot->reset(); }
    float operator()() noexcept { return 1.f; }
};
} // namespace
int main()
{
    absent<0>(); absent<1>(); absent<2>();
    char fieldSchema[4096], commandSchema[4096], current[4096];
    const auto fieldSize = writeSchema(fields, fieldSchema, sizeof fieldSchema);
    const auto commandSize = writeSchema(actions, commandSchema, sizeof commandSchema);
    expect(fieldSize && commandSize, "schemas exist while all slots are empty");
    Meter first, second;
    contextRead.bind(&readContext, &first); contextWrite.bind(&writeContext, &first); contextCall.bind(&callContext, &first);
    borrowedRead.bind<&Meter::read>(first); borrowedWrite.bind<&Meter::write>(first); borrowedCall.bind<&Meter::call>(first);
    ownedRead.bind([&first]() noexcept { return first.read(); });
    ownedWrite.bind([&first](float v) noexcept { return first.write(v); });
    ownedCall.bind([&first](float v, Mode m) noexcept { return first.call(v, m); });
    expect(contextRead.available() && borrowedRead.available() && ownedRead.available(), "common availability contract");
    roundTrip<0>(first); roundTrip<1>(first); roundTrip<2>(first);
    contextRead.bind(&readContext, &second); contextWrite.bind(&writeContext, &second); contextCall.bind(&callContext, &second);
    auto read = [&second]() noexcept { return second.read(); };
    auto write = [&second](float v) noexcept { return second.write(v); };
    auto call = [&second](float v, Mode m) noexcept { return second.call(v, m); };
    borrowedRead.bind(read); borrowedWrite.bind(write); borrowedCall.bind(call);
    ownedRead.bind(read); ownedWrite.bind(write); ownedCall.bind(call);
    const int oldCalls = first.calls, oldWrites = first.writes, oldReads = first.reads;
    roundTrip<0>(second); roundTrip<1>(second); roundTrip<2>(second);
    expect(first.calls == oldCalls && first.writes == oldWrites && first.reads == oldReads, "rebind no longer accesses old objects");
    expect(writeSchema(fields, current, sizeof current) == fieldSize && !std::strcmp(fieldSchema, current), "field schema and CRC stable");
    expect(writeSchema(actions, current, sizeof current) == commandSize && !std::strcmp(commandSchema, current), "command schema and CRC stable");
    contextRead.bind([](void* p) noexcept { return p ? 1.f : 0.f; }, nullptr);
    expect(contextRead && rows.read<0>() == 0.f, "null context may be meaningful to a present function");
    const auto& constRead = ownedRead;
    const auto& constWrite = borrowedWrite;
    FieldTable mixed{field("Mixed", "", constRead, constWrite)};
    expect(mixed.read<0>() == second.value && mixed.write<0>(33) == WriteResult::Applied && second.value == 33.f,
           "const views and different slot kinds pair naturally");
    contextRead.reset(); contextWrite.reset(); contextCall.reset();
    borrowedRead.reset(); borrowedWrite.reset(); borrowedCall.reset();
    ownedRead.reset(); ownedWrite.reset(); ownedCall.reset();
    absent<0>(); absent<1>(); absent<2>();
    borrowedRead.bind<&readConstant>();
    expect(rows.read<1>() == 17.f, "borrowed NTTP free function");
    borrowedRead.bind(&readConstant);
    expect(borrowedRead.invoke() == 17.f, "borrowed runtime free function");
    borrowedRead.bind([]() noexcept { return 18.f; });
    expect(rows.read<1>() == 18.f, "capture-free temporary converts to a stored function pointer");
    borrowedRead.bind(nullptr);
    expect(!borrowedRead, "null function resets borrowed slot without delegate assertion");
    ownedRead.bind(readConstant);
    expect(rows.read<2>() == 17.f, "owned free function lvalue");
    ownedRead.bind(static_cast<float(*)() noexcept>(nullptr));
    expect(!ownedRead, "null typed function resets owned slot");
    int live = 0;
    {
        Tracked target(live, 42.f);
        ownedRead.bind(target);
        expect(live == 2 && rows.read<2>() == 42.f, "owned callable copied into inline storage");
        target.value = 99.f;
        expect(rows.read<2>() == 42.f, "owned closure state independent of source");
    }
    expect(live == 1 && rows.read<2>() == 42.f, "owned callable survives source destruction");
    ownedRead.bind([value = std::make_unique<float>(81.f)]() noexcept { return *value; });
    expect(live == 0 && rows.read<2>() == 81.f, "replacement destroys old target and supports move-only capture");
    ownedRead.bind(nullptr);
    expect(!ownedRead, "owned reset clears availability");
    int state = 5;
    auto external = [&state]() noexcept { return float(++state); };
    borrowedRead.bind(external);
    borrowedRead.reset();
    expect(external() == 6.f, "borrowed reset does not destroy external closure");
    DelegateSlot<Mode() noexcept, 32> enumRead;
    DelegateRefSlot<WriteResult(Mode) noexcept> enumWrite;
    Mode mode = Mode::Auto;
    auto modeWrite = [&mode](Mode v) noexcept { mode = v; return WriteResult::Applied; };
    enumRead.bind([&mode]() noexcept { return mode; }); enumWrite.bind(modeWrite);
    FieldTable enums{field("Mode", "", enumRead, enumWrite)};
    expect(enums.read<0>() == 1 && enums.write<0>(2) == WriteResult::Applied && mode == Mode::Manual
           && enums.write<0>(3) == WriteResult::InvalidValue, "enum inference and bounds across slot kinds");
    DelegateSlot<Scalar() noexcept, 32> scalarRead;
    ContextFunctionSlot<WriteResult(const Scalar&) noexcept> scalarWrite;
    scalarRead.bind([]() noexcept { return Scalar::fromF64(12.75); });
    scalarWrite.bind([](void* p, const Scalar& value) noexcept { *static_cast<float*>(p) = value.get<float>(); return WriteResult::Applied; }, &second.value);
    FieldTable scalars{field("Scalar", "", ScalarType::F32, scalarRead, scalarWrite)};
    expect(scalars.read<0>().get<float>() == 12.75f && scalars.write<0>(57) == WriteResult::Applied
           && second.value == 57.f, "explicit Scalar callbacks retain declared-type normalization");
    enumRead.reset(); enumWrite.reset(); scalarWrite.reset();
    DelegateSlot<Self&() noexcept> self;
    self.bind(Self{});
    self.invoke().value = 456;
    self.bind(self.invoke());
    expect(self.invoke().value == 456, "replacement preserves an alias of its own stored target before destruction");
    ownedRead.bind(ResetOnDestroy{ownedRead});
    ownedRead.reset();
    expect(!ownedRead, "target destructor may reset its already-cleared slot");
    DelegateSlot<void() noexcept> event;
    int events = 0;
    event.bind([&events]() noexcept { return ++events; });
    event.invoke();
    expect(events == 1, "generic void slot discards callback result without changing noexcept");
    struct Prefix { virtual ~Prefix() = default; int pad = 0; };
    struct Base { virtual float value() const noexcept { return 1.f; } };
    struct Derived : Prefix, Base { float value() const noexcept override { return 2.f; } } derived;
    const auto& constDerived = derived;
    borrowedRead.bind<&Base::value>(constDerived);
    expect(rows.read<1>() == 2.f, "borrowed method retains const/base adjustment and virtual dispatch");
    borrowedRead.reset();
    auto mutableGetter = [count = 0]() mutable noexcept { return float(++count); };
    ownedRead.bind(mutableGetter);
    expect(rows.read<2>() == 1.f && rows.read<2>() == 2.f && mutableGetter() == 1.f,
           "mutable owned state is retained and does not mutate the source closure");
    ownedRead.reset();
    DelegateSlot<int(int) noexcept> generic;
    generic.bind([increment = 2](auto value) noexcept { return value + increment; });
    expect(generic.invoke(3) == 5, "generic closure specializes to the exact slot signature");
    struct Overloaded {
        int operator()(int v) noexcept { return v + 1; }
        float operator()(float v) noexcept { return v + 2.f; }
    } overload;
    DelegateRefSlot<int(int) noexcept> overloaded;
    overloaded.bind(overload);
    expect(overloaded.invoke(3) == 4, "slot signature selects an exact callable overload");
    std::printf("%d/%d late-bound checks passed\n", checks - failures, checks);
    return failures ? 1 : 0;
}
