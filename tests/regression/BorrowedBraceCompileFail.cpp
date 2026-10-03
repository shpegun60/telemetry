// Preserve shared slot braced-owner and target-view case numbers (MIT).
#include <telemetry/slot/TelemetryOwnerSlot.h>
#include <telemetry/slot/TelemetryFunctionSlot.h>
#include <telemetry/slot/TelemetryContextFunctionSlot.h>
#include <telemetry/slot/TelemetryDelegateRefSlot.h>
#include <telemetry/slot/TelemetryDelegateSlot.h>
#include "SharedSupport.hpp"
using namespace telemetry;
struct Owner { int value = 9; int read() const noexcept { return value; } };
struct Prefix { int prefix = 5; };
struct Derived : Prefix, Owner {};
const Owner owner{};
struct Read { const Owner* source = &owner; int operator()() const noexcept { return source->read(); } };
struct ConvertedOwner : Owner { ConvertedOwner() noexcept {} };
struct ConvertedRead : Read { ConvertedRead() noexcept {} };
template <class T> struct Proxy { operator T() const noexcept { return {}; } };
Proxy<ConvertedOwner> ownerProxy;
struct Convertible { int value = 17; using Function = int(*)() noexcept; static int constant() noexcept { return 71; } operator Function() const noexcept { return &constant; } int operator()() const noexcept { return value; } };
#if TELEMETRY_BORROWED_BRACE_FAIL_CASE == 43
void invalid() { OwnerSlot<const Owner> slot; slot.bind(ownerProxy); }
#elif TELEMETRY_BORROWED_BRACE_FAIL_CASE == 44
void invalid() { OwnerSlot<const ConvertedOwner> slot; slot.bind({ownerProxy}); }
#elif TELEMETRY_BORROWED_BRACE_FAIL_CASE == 45
void invalid() { OwnerSlot<const ConvertedOwner> slot; slot.bind({Proxy<ConvertedOwner>{}}); }
#elif TELEMETRY_BORROWED_BRACE_FAIL_CASE == 46
void invalid() { DelegateRefSlot<int() noexcept> slot; slot.bind<const Convertible>(Convertible{}); }
#elif TELEMETRY_BORROWED_BRACE_FAIL_CASE == 47
void invalid() { DelegateRefSlot<int() noexcept> slot; slot.bind<const Convertible>({}); }
#elif TELEMETRY_BORROWED_BRACE_FAIL_CASE == 48
void invalid() { DelegateRefSlot<int() noexcept> slot; slot.bind<const Convertible>({7}); }
#elif TELEMETRY_BORROWED_BRACE_FAIL_CASE == 49
void invalid() { DelegateRefSlot<int() noexcept> slot; slot.bind<&Owner::read, const ConvertedOwner>({ownerProxy}); }
#elif TELEMETRY_BORROWED_BRACE_FAIL_CASE == 50
void invalid() { DelegateRefSlot<int() noexcept> slot; slot.bind<const ConvertedRead>({Proxy<ConvertedRead>{}}); }
#elif TELEMETRY_BORROWED_BRACE_FAIL_CASE == 51
auto invalid = DelegateRefSlot<int() noexcept>{}.get();
#elif TELEMETRY_BORROWED_BRACE_FAIL_CASE == 52
void invalid() { DelegateRefSlot<int() noexcept> slot; auto target = std::move(slot).get(); (void)target; }
#elif TELEMETRY_BORROWED_BRACE_FAIL_CASE == 53
void invalid() { const DelegateRefSlot<int() noexcept> slot; auto target = std::move(slot).get(); (void)target; }
#elif TELEMETRY_BORROWED_BRACE_FAIL_CASE == 54
auto invalid = DelegateSlot<int() noexcept>{}.get();
#elif TELEMETRY_BORROWED_BRACE_FAIL_CASE == 55
void invalid() { DelegateSlot<int() noexcept> slot; auto target = std::move(slot).get(); (void)target; }
#elif TELEMETRY_BORROWED_BRACE_FAIL_CASE == 56
void invalid() { const DelegateSlot<int() noexcept> slot; auto target = std::move(slot).get(); (void)target; }
#elif TELEMETRY_BORROWED_BRACE_FAIL_CASE == 0
int main() {
    Owner live{13}; Derived derived; derived.value = 29; const Derived fixed{};
    OwnerSlot<const Owner> ownerSlot;
    ownerSlot.bind({live}); CHECK(ownerSlot.get() == &live);
    ownerSlot.bind({derived}); CHECK(ownerSlot.get() == static_cast<const Owner*>(&derived));
    ownerSlot.bind({fixed}); CHECK(ownerSlot.get() == static_cast<const Owner*>(&fixed));
    Read read{&live}; DelegateRefSlot<int() noexcept> borrowed;
    borrowed.bind<&Owner::read, const Owner>({live}); CHECK(borrowed.invoke() == 13);
    borrowed.bind<&Owner::read, const Owner>({derived}); CHECK(borrowed.invoke() == 29);
    borrowed.bind<const Read>({read}); CHECK(borrowed.invoke() == 13);
    auto target = borrowed.get(); CHECK(target && target.invoke() == 13);
    borrowed.bind([]() noexcept { return 53; }); CHECK(target.invoke() == 53);
    borrowed.bind(Convertible{}); CHECK(borrowed.invoke() == Convertible::constant());
    const Convertible converted{19}; borrowed.bind<const Convertible>({converted}); CHECK(borrowed.invoke() == 19);
    CHECK(std::as_const(borrowed).get().invoke() == 19);
    DelegateSlot<int() noexcept> owned; owned.bind([value = 61]() noexcept { return value; });
    auto ownedTarget = owned.get(); CHECK(ownedTarget && ownedTarget.invoke() == 61);
    CHECK(std::as_const(owned).get().invoke() == 61);
    CHECK(!ContextFunctionSlot<int() noexcept>{}.get());
    CHECK(FunctionSlot<int() noexcept>{}.get() == nullptr);
    reportChecks();
}
#else
#error "Select maintained shared brace case"
#endif
#if TELEMETRY_BORROWED_BRACE_FAIL_CASE != 0
int main() {}
#endif
