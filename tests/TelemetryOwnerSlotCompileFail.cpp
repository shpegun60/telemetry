// Lifetime and owner-type errors must fail before a table can retain an address.
// Authors: Ruslan Kovtun (shpegun60), codexAi. License: MIT.
#include <telemetry/Telemetry.hpp>
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
#error "Select a maintained shared case"
#endif
int main() {}
