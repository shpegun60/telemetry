// Low-level owner, function, context and delegate slot lifecycle contracts (MIT).
#include <telemetry/slot/TelemetryOwnerSlot.h>
#include <telemetry/slot/TelemetryFunctionSlot.h>
#include <telemetry/slot/TelemetryContextFunctionSlot.h>
#include <telemetry/slot/TelemetryDelegateRefSlot.h>
#include <telemetry/slot/TelemetryDelegateSlot.h>
#include <telemetry/result/EndpointStatus.hpp>
#include "SharedSupport.hpp"
#include <cstdint>
#include <limits>
#include <memory>
using namespace telemetry;
namespace {
struct Owner {
    float value = 23;
    unsigned reads = 0, writes = 0, calls = 0;
    float read() noexcept { ++reads; return value; }
    float readConst() const noexcept { return value; }
    WriteResult write(float next) noexcept { ++writes; value = next; return WriteResult::Busy; }
    CommandResult call(float next) noexcept { ++calls; value = next; return CommandResult::Accepted; }
    Owner* operator&() noexcept { return nullptr; }
};
float first() noexcept { return 1; }
float second() noexcept { return 2; }
FunctionSlot<float() noexcept> function;
float rebindFunction() noexcept { function.bind(&second); return 19; }
float contextRead(void* context) noexcept { return static_cast<Owner*>(context)->read(); }
WriteResult contextWrite(void* context, float value) noexcept { return static_cast<Owner*>(context)->write(value); }
CommandResult contextCall(void* context, float value) noexcept { return static_cast<Owner*>(context)->call(value); }
struct Tracked {
    int* live;
    float value;
    Tracked(int& count, float next) noexcept : live(&count), value(next) { ++*live; }
    Tracked(const Tracked& other) noexcept : live(other.live), value(other.value) { ++*live; }
    Tracked(Tracked&& other) noexcept : live(other.live), value(other.value) { ++*live; }
    ~Tracked() noexcept { --*live; }
    float operator()() noexcept { return value; }
};
struct Self { int value = 123; Self& operator()() noexcept { return *this; } };
using Owned = DelegateSlot<float() noexcept>;
struct ResetOnDestroy {
    Owned* slot;
    explicit ResetOnDestroy(Owned& target) noexcept : slot(&target) {}
    ResetOnDestroy(ResetOnDestroy&& other) noexcept : slot(other.slot) { other.slot = nullptr; }
    ~ResetOnDestroy() noexcept { if (slot) slot->reset(); }
    float operator()() noexcept { return 1; }
};
constexpr bool constantFunction() noexcept {
    FunctionSlot<float() noexcept> local;
    if (local || local.get() != nullptr) return false;
    local.bind(&first);
    if (!local || local.get() != &first) return false;
    local.reset(); local.bind(nullptr); return !local;
}
static_assert(constantFunction());
static_assert(sizeof(OwnerSlot<Owner>) == sizeof(Owner*));
static_assert(sizeof(FunctionSlot<float() noexcept>) == sizeof(float(*)() noexcept));
static_assert(sizeof(ContextFunctionSlot<float() noexcept>) == 2 * sizeof(void*));
static_assert(sizeof(DelegateRefSlot<float() noexcept>) == 2 * sizeof(void*));
static_assert(sizeof(Owned) == sizeof(tiny::delegate<float(), 32>));
static_assert(!std::is_copy_constructible_v<OwnerSlot<Owner>> && !std::is_move_constructible_v<OwnerSlot<Owner>>);
static_assert(!std::is_copy_constructible_v<Owned> && !std::is_move_constructible_v<Owned>);
static_assert(!std::is_invocable_v<Owned&>);
static_assert(!std::is_default_constructible_v<decltype(std::declval<Owned&>().get())>);
}
int main() {
    Owner a, b; b.value = 40;
    OwnerSlot<Owner> owner;
    CHECK(!owner && owner.get() == nullptr);
    owner.bind(a); CHECK(owner && owner.get() == std::addressof(a));
    CHECK(owner.get()->read() == 23 && a.reads == 1);
    owner.bind(b); CHECK(owner.get()->write(41) == WriteResult::Busy && b.value == 41 && a.writes == 0);
    owner.reset(); CHECK(!owner && owner.get() == nullptr);
    const Owner immutable;
    OwnerSlot<const Owner> constOwner;
    constOwner.bind(immutable); CHECK(constOwner.get()->readConst() == 23);
    struct Prefix { virtual ~Prefix() = default; std::uint64_t marker = UINT64_C(0x12345678); };
    struct Base { virtual float read() const noexcept { return -1; } virtual ~Base() = default; };
    struct Derived : Prefix, Base { float read() const noexcept override { return 42; } } derived;
    OwnerSlot<Base> base; OwnerSlot<Derived> exact;
    base.bind(derived); exact.bind(derived);
    CHECK(base.get() == static_cast<Base*>(std::addressof(derived)) && base.get()->read() == 42);
    CHECK(exact.get()->read() == 42 && derived.marker == UINT64_C(0x12345678));
    CHECK(!function && function.get() == nullptr);
    function.bind(first); CHECK(function.invoke() == 1);
    function.bind(&second); const auto& functionView = function; CHECK(functionView.invoke() == 2);
    function.bind(&rebindFunction); CHECK(function.invoke() == 19 && function.invoke() == 2);
    function.reset(); CHECK(!function); function.bind(nullptr); CHECK(!function);
    enum class Mode : std::uint16_t { Off, Auto, Manual };
    Mode mode = Mode::Auto;
    FunctionSlot<Mode() noexcept> modeRead;
    FunctionSlot<WriteResult(Mode) noexcept> modeWrite;
    static Mode selectedMode = Mode::Auto;
    modeRead.bind([]() noexcept { return selectedMode; });
    modeWrite.bind([](Mode next) noexcept { selectedMode = next; return WriteResult::Busy; });
    CHECK(modeRead.invoke() == Mode::Auto);
    CHECK(modeWrite.invoke(Mode::Manual) == WriteResult::Busy && modeRead.invoke() == Mode::Manual);
    DelegateSlot<Mode() noexcept> ownedMode;
    DelegateRefSlot<WriteResult(Mode) noexcept> borrowedMode;
    auto changeMode = [&mode](Mode next) noexcept { mode = next; return WriteResult::Applied; };
    ownedMode.bind([&mode]() noexcept { return mode; }); borrowedMode.bind(changeMode);
    CHECK(ownedMode.invoke() == Mode::Auto);
    CHECK(borrowedMode.invoke(Mode::Manual) == WriteResult::Applied && ownedMode.invoke() == Mode::Manual);
    ownedMode.reset(); borrowedMode.reset(); CHECK(!ownedMode && !borrowedMode);
    FunctionSlot<std::uint64_t() noexcept> wide;
    wide.bind([]() noexcept { return UINT64_MAX; }); CHECK(wide.invoke() == UINT64_MAX);
    ContextFunctionSlot<float() noexcept> context;
    ContextFunctionSlot<WriteResult(float) noexcept> writer;
    ContextFunctionSlot<CommandResult(float) noexcept> call;
    DelegateRefSlot<float() noexcept> borrowed;
    DelegateRefSlot<WriteResult(float) noexcept> borrowedWriter;
    DelegateRefSlot<CommandResult(float) noexcept> borrowedCall;
    Owned owned;
    DelegateSlot<WriteResult(float) noexcept> ownedWriter;
    DelegateSlot<CommandResult(float) noexcept> ownedCall;
    CHECK(!context && !writer && !call && !borrowed && !owned);
    const auto roundTrip = [&](Owner& target) {
        const auto beforeReads = target.reads, beforeWrites = target.writes, beforeCalls = target.calls;
        CHECK(context.invoke() == target.value && borrowed.invoke() == target.value && owned.invoke() == target.value);
        CHECK(target.reads == beforeReads + 3);
        CHECK(writer.invoke(110) == WriteResult::Busy && borrowedWriter.invoke(115) == WriteResult::Busy && ownedWriter.invoke(120) == WriteResult::Busy);
        CHECK(target.writes == beforeWrites + 3 && target.value == 120);
        CHECK(call.invoke(125) == CommandResult::Accepted && borrowedCall.invoke(130) == CommandResult::Accepted && ownedCall.invoke(135) == CommandResult::Accepted);
        CHECK(target.calls == beforeCalls + 3 && target.value == 135);
    };
    const auto bind = [&](Owner& target) {
        context.bind(&contextRead, std::addressof(target)); writer.bind(&contextWrite, std::addressof(target)); call.bind(&contextCall, std::addressof(target));
        borrowed.bind<&Owner::read>(target); borrowedWriter.bind<&Owner::write>(target); borrowedCall.bind<&Owner::call>(target);
        owned.bind([&target]() noexcept { return target.read(); });
        ownedWriter.bind([&target](float next) noexcept { return target.write(next); });
        ownedCall.bind([&target](float next) noexcept { return target.call(next); });
    };
    bind(a); roundTrip(a);
    const auto oldReads = a.reads, oldWrites = a.writes, oldCalls = a.calls;
    bind(b); roundTrip(b); CHECK(a.reads == oldReads && a.writes == oldWrites && a.calls == oldCalls);
    context.bind([](void* p) noexcept { return p ? 1.f : 0.f; }, nullptr); CHECK(context && context.invoke() == 0);
    const auto& ownedView = owned; CHECK(ownedView.invoke() == b.value);
    context.reset(); writer.reset(); call.reset(); borrowed.reset(); borrowedWriter.reset(); borrowedCall.reset(); owned.reset(); ownedWriter.reset(); ownedCall.reset();
    CHECK(!context && !writer && !call && !borrowed && !borrowedWriter && !borrowedCall && !owned && !ownedWriter && !ownedCall);
    borrowed.bind<&first>(); CHECK(borrowed.invoke() == 1);
    borrowed.bind(&second); CHECK(borrowed.invoke() == 2);
    borrowed.bind([]() noexcept { return 18.f; }); CHECK(borrowed.invoke() == 18);
    borrowed.bind(nullptr); CHECK(!borrowed);
    owned.bind(first); CHECK(owned.invoke() == 1);
    owned.bind(static_cast<float(*)() noexcept>(nullptr)); CHECK(!owned);
    int live = 0;
    { Tracked target(live, 42); owned.bind(target); CHECK(live == 2 && owned.invoke() == 42); target.value = 99; CHECK(owned.invoke() == 42); }
    CHECK(live == 1 && owned.invoke() == 42);
    owned.bind([value = std::make_unique<float>(81)]() noexcept { return *value; }); CHECK(live == 0 && owned.invoke() == 81);
    owned.bind(nullptr); CHECK(!owned);
    int state = 5; auto external = [&state]() noexcept { return float(++state); };
    borrowed.bind(external); borrowed.reset(); CHECK(external() == 6);
    DelegateSlot<Self&() noexcept> self; self.bind(Self{}); self.invoke().value = 456; self.bind(self.invoke()); CHECK(self.invoke().value == 456);
    owned.bind(ResetOnDestroy{owned}); owned.reset(); CHECK(!owned);
    DelegateSlot<void() noexcept> event; int events = 0; event.bind([&events]() noexcept { return ++events; }); event.invoke(); CHECK(events == 1);
    borrowed.bind<&Base::read>(std::as_const(derived)); CHECK(borrowed.invoke() == 42); borrowed.reset();
    auto mutableGetter = [count = 0]() mutable noexcept { return float(++count); }; owned.bind(mutableGetter);
    CHECK(owned.invoke() == 1 && owned.invoke() == 2 && mutableGetter() == 1); owned.reset();
    DelegateSlot<int(int) noexcept> generic; generic.bind([increment = 2](auto value) noexcept { return value + increment; }); CHECK(generic.invoke(3) == 5);
    struct Overloaded { int operator()(int v) noexcept { return v + 1; } float operator()(float v) noexcept { return v + 2; } } overload;
    DelegateRefSlot<int(int) noexcept> overloaded; overloaded.bind(overload); CHECK(overloaded.invoke(3) == 4);
    reportChecks();
}
