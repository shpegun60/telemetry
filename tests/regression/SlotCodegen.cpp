// Native slot-backed reads compared with explicit selected-target calls (MIT).
#include <telemetry/Telemetry.hpp>
using namespace telemetry;
FunctionSlot<int() noexcept> functionRead;
ContextFunctionSlot<int() noexcept> contextRead;
DelegateRefSlot<int() noexcept> borrowedRead;
DelegateSlot<int() noexcept> ownedRead;
struct Owner { int read() const noexcept; };
OwnerSlot<Owner> ownerRead;
constexpr FieldTable functions{field("value", functionRead)};
constexpr FieldTable contexts{field("value", contextRead)};
constexpr FieldTable borrowed{field("value", borrowedRead)};
constexpr FieldTable owned{field("value", ownedRead)};
constexpr FieldTable owners{field<&Owner::read>("value", ownerRead)};
extern "C" {
std::optional<int> slot_function_direct() noexcept { auto target = functionRead.get(); if (!target) return std::nullopt; return target(); }
std::optional<int> slot_function_table() noexcept { return functions.read<0>(); }
std::optional<int> slot_context_direct() noexcept { auto target = contextRead.get(); if (!target) return std::nullopt; return target.invoke(); }
std::optional<int> slot_context_table() noexcept { return contexts.read<0>(); }
std::optional<int> slot_borrowed_direct() noexcept { auto target = borrowedRead.get(); if (!target) return std::nullopt; return target.invoke(); }
std::optional<int> slot_borrowed_table() noexcept { return borrowed.read<0>(); }
std::optional<int> slot_owned_direct() noexcept { auto target = ownedRead.get(); if (!target) return std::nullopt; return target.invoke(); }
std::optional<int> slot_owned_table() noexcept { return owned.read<0>(); }
std::optional<int> slot_owner_direct() noexcept { const auto* target = ownerRead.get(); return detail::targetAvailable<&Owner::read>() && target ? std::optional<int>{target->read()} : std::nullopt; }
std::optional<int> slot_owner_table() noexcept { return owners.read<0>(); }
}
