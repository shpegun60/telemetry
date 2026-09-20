// Slot-backed typed calls must match one handwritten load, check and call.
// Authors: Ruslan Kovtun (shpegun60), codexAi. License: MIT.
#include "Telemetry.h"
using namespace telemetry;
extern FunctionSlot<float() noexcept> functionProbeRead;
extern FunctionSlot<std::uint16_t() noexcept> functionProbeCount;
extern FunctionSlot<WriteResult(std::uint16_t) noexcept> functionProbeWrite;
extern FunctionSlot<CommandResult(std::uint16_t) noexcept> functionProbeCall;
extern constexpr FieldTable functionProbeFields{
    field("Value", "", functionProbeRead),
    field("Count", "", functionProbeCount, functionProbeWrite)};
constexpr FieldCatalogTable functionProbeCatalog{group("slot", functionProbeFields)};
extern constexpr CommandTable functionProbeCommands{command("Call", functionProbeCall)};
constexpr CommandCatalogTable functionProbeActions{group("slot", functionProbeCommands)};
static_assert(sizeof(functionProbeRead) == 4 && sizeof(functionProbeCall) == 4);
static_assert(sizeof(Field) == 96 && alignof(Field) == 32 && sizeof(Command) == 20);

extern "C" {
float function_slot_manual_read() noexcept
{
    auto fn = functionProbeRead.get();
    return fn != nullptr ? fn() : -1.f;
}
float function_slot_local_read() noexcept { return functionProbeFields.read<0>().value_or(-1.f); }
float function_slot_global_read() noexcept { return functionProbeCatalog.read<0>().value_or(-1.f); }
WriteResult function_slot_manual_write(std::uint16_t value) noexcept
{
    auto fn = functionProbeWrite.get();
    return fn != nullptr ? fn(value) : WriteResult::Unavailable;
}
WriteResult function_slot_local_write(std::uint16_t value) noexcept { return functionProbeFields.write<1>(value); }
WriteResult function_slot_global_write(std::uint16_t value) noexcept { return functionProbeCatalog.write<1>(value); }
CommandResult function_slot_manual_call(std::uint16_t value) noexcept
{
    auto fn = functionProbeCall.get();
    return fn != nullptr ? fn(value) : CommandResult::Unavailable;
}
CommandResult function_slot_local_call(std::uint16_t value) noexcept { return functionProbeCommands.call<0>(value); }
CommandResult function_slot_global_call(std::uint16_t value) noexcept { return functionProbeActions.call<0>(value); }
CommandResult function_slot_manual_convert(double value) noexcept
{
    auto fn = functionProbeCall.get();
    if (!fn) return CommandResult::Unavailable;
    std::uint16_t converted{};
    if (!detail::convertNumberTo(value, converted)) return CommandResult::InvalidValue;
    return fn(converted);
}
CommandResult function_slot_local_convert(double value) noexcept { return functionProbeCommands.call<0>(value); }
CommandResult function_slot_global_convert(double value) noexcept { return functionProbeActions.call<0>(value); }
}
