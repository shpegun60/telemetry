// Compare late-bound native operations with explicit pointer checks on Cortex-M7.
// Authors: Ruslan Kovtun (shpegun60), codexAi. License: MIT.
#include "Telemetry.h"
using namespace telemetry;

struct SlotProbeOwner {
    float read() const noexcept;
    WriteResult write(std::uint16_t) noexcept;
    std::uint16_t count() const noexcept;
    CommandResult call(std::uint16_t) noexcept;
};
extern OwnerSlot<SlotProbeOwner> slotProbeOwner;
extern SlotProbeOwner directProbeOwner;
float slotProbeFreeRead() noexcept;
WriteResult slotProbeFreeWrite(std::uint16_t) noexcept;
std::uint16_t slotProbeFreeCount() noexcept;
CommandResult slotProbeFreeCall(std::uint16_t) noexcept;

extern constexpr FieldTable slotProbeFields{
    field<&SlotProbeOwner::read>("Value", "", slotProbeOwner),
    field<&SlotProbeOwner::count, &SlotProbeOwner::write>("Count", "", slotProbeOwner)};
constexpr FieldCatalogTable slotProbeCatalog{group("slot", slotProbeFields)};
extern constexpr CommandTable slotProbeCommands{
    command<&SlotProbeOwner::call>("Call", slotProbeOwner)};
constexpr CommandCatalogTable slotProbeActions{group("slot", slotProbeCommands)};
constexpr FieldTable directProbeFields{
    field<&SlotProbeOwner::read>("Value", "", directProbeOwner),
    field<&SlotProbeOwner::count, &SlotProbeOwner::write>("Count", "", directProbeOwner)};
constexpr CommandTable directProbeCommands{command<&SlotProbeOwner::call>("Call", directProbeOwner)};
constexpr FieldTable freeProbeFields{
    field<&slotProbeFreeRead>("Value", ""),
    field<&slotProbeFreeCount, &slotProbeFreeWrite>("Count", "")};
constexpr CommandTable freeProbeCommands{command<&slotProbeFreeCall>("Call")};
static_assert(sizeof(OwnerSlot<SlotProbeOwner>) == 4);
static_assert(sizeof(Field) == 96 && alignof(Field) == 32 && sizeof(Command) == 20);

extern "C" {
float slot_manual_read() noexcept
{
    auto* owner = slotProbeOwner.get();
    return owner != nullptr ? owner->read() : -1.f;
}
float slot_local_read() noexcept { return slotProbeFields.read<0>().value_or(-1.f); }
float slot_global_read() noexcept { return slotProbeCatalog.read<0>().value_or(-1.f); }
WriteResult slot_manual_write(std::uint16_t value) noexcept
{
    auto* owner = slotProbeOwner.get();
    return owner != nullptr ? owner->write(value) : WriteResult::Unavailable;
}
WriteResult slot_local_write(std::uint16_t value) noexcept { return slotProbeFields.write<1>(value); }
WriteResult slot_global_write(std::uint16_t value) noexcept { return slotProbeCatalog.write<1>(value); }
CommandResult slot_manual_call(std::uint16_t value) noexcept
{
    auto* owner = slotProbeOwner.get();
    return owner != nullptr ? owner->call(value) : CommandResult::Unavailable;
}
CommandResult slot_local_call(std::uint16_t value) noexcept { return slotProbeCommands.call<0>(value); }
CommandResult slot_global_call(std::uint16_t value) noexcept { return slotProbeActions.call<0>(value); }

float slot_direct_manual_read() noexcept { return directProbeOwner.read(); }
float slot_direct_table_read() noexcept { return directProbeFields.read<0>().value_or(-1.f); }
WriteResult slot_direct_manual_write(std::uint16_t value) noexcept { return directProbeOwner.write(value); }
WriteResult slot_direct_table_write(std::uint16_t value) noexcept { return directProbeFields.write<1>(value); }
CommandResult slot_direct_manual_call(std::uint16_t value) noexcept { return directProbeOwner.call(value); }
CommandResult slot_direct_table_call(std::uint16_t value) noexcept { return directProbeCommands.call<0>(value); }
float slot_free_manual_read() noexcept { return slotProbeFreeRead(); }
float slot_free_table_read() noexcept { return freeProbeFields.read<0>().value_or(-1.f); }
WriteResult slot_free_manual_write(std::uint16_t value) noexcept { return slotProbeFreeWrite(value); }
WriteResult slot_free_table_write(std::uint16_t value) noexcept { return freeProbeFields.write<1>(value); }
CommandResult slot_free_manual_call(std::uint16_t value) noexcept { return slotProbeFreeCall(value); }
CommandResult slot_free_table_call(std::uint16_t value) noexcept { return freeProbeCommands.call<0>(value); }
}
