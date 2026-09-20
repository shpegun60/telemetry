// Lifetime and owner-type errors must fail before a table can retain an address.
// Authors: Ruslan Kovtun (shpegun60), codexAi. License: MIT.
#include "Telemetry.h"
using namespace telemetry;
struct Owner {
    float read() noexcept { return 1; }
    CommandResult call() noexcept { return CommandResult::Executed; }
};
struct Derived : Owner {};
struct Other {};
OwnerSlot<Owner> slot;

#if TELEMETRY_OWNER_SLOT_FAIL_CASE == 1
void bad() { slot.bind(Owner{}); }
#elif TELEMETRY_OWNER_SLOT_FAIL_CASE == 2
void bad() { OwnerSlot<const Owner> target; target.bind(Owner{}); }
#elif TELEMETRY_OWNER_SLOT_FAIL_CASE == 3
void bad() { OwnerSlot<const Owner> target; target.bind(Derived{}); }
#elif TELEMETRY_OWNER_SLOT_FAIL_CASE == 4
auto bad = field<&Owner::read>("", "", OwnerSlot<Owner>{});
#elif TELEMETRY_OWNER_SLOT_FAIL_CASE == 5
auto bad = command<&Owner::call>("", OwnerSlot<Owner>{});
#elif TELEMETRY_OWNER_SLOT_FAIL_CASE == 6
auto bad = field<&Owner::read, nullptr, const OwnerSlot<Owner>>("", "", OwnerSlot<Owner>{});
#elif TELEMETRY_OWNER_SLOT_FAIL_CASE == 7
auto bad = command<&Owner::call, const OwnerSlot<Owner>>("", OwnerSlot<Owner>{});
#elif TELEMETRY_OWNER_SLOT_FAIL_CASE == 8
OwnerSlot<const Owner> target;
auto bad = field<&Owner::read>("", "", target);
#elif TELEMETRY_OWNER_SLOT_FAIL_CASE == 9
OwnerSlot<Other> target;
CommandTable bad{command<&Owner::call>("", target)};
#elif TELEMETRY_OWNER_SLOT_FAIL_CASE == 10
auto bad = slot;
#elif TELEMETRY_OWNER_SLOT_FAIL_CASE == 11
void bad() { OwnerSlot<Owner> target; target = slot; }
#elif TELEMETRY_OWNER_SLOT_FAIL_CASE == 12
auto bad = std::move(slot);
#elif TELEMETRY_OWNER_SLOT_FAIL_CASE == 13
OwnerSlot<void> bad;
#elif TELEMETRY_OWNER_SLOT_FAIL_CASE == 14
OwnerSlot<volatile Owner> bad;
#elif TELEMETRY_OWNER_SLOT_FAIL_CASE == 15
void bad() { OwnerSlot<const Owner> target; target.bind<const Owner>(Owner{}); }
#elif TELEMETRY_OWNER_SLOT_FAIL_CASE == 16
void bad() { Other other; slot.bind(other); }
#elif TELEMETRY_OWNER_SLOT_FAIL_CASE == 17
void bad() { slot.bind(static_cast<Owner*>(nullptr)); }
#elif TELEMETRY_OWNER_SLOT_FAIL_CASE == 18
void bad() { OwnerSlot<Owner> target; target = std::move(slot); }
#else
#error Select TELEMETRY_OWNER_SLOT_FAIL_CASE from 1 through 18
#endif
int main() {}
