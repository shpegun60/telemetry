// Late binding adds only each strategy's checked native callback dispatch.
// Authors: Ruslan Kovtun (shpegun60), codexAi. License: MIT.
#include "Telemetry.h"
using namespace telemetry;
extern ContextFunctionSlot<float() noexcept> contextProbeRead;
extern ContextFunctionSlot<WriteResult(std::uint16_t) noexcept> contextProbeWrite;
extern ContextFunctionSlot<CommandResult(std::uint16_t) noexcept> contextProbeCall;
extern DelegateRefSlot<float() noexcept> refProbeRead;
extern DelegateRefSlot<WriteResult(std::uint16_t) noexcept> refProbeWrite;
extern DelegateRefSlot<CommandResult(std::uint16_t) noexcept> refProbeCall;
extern DelegateSlot<float() noexcept, 32> ownedProbeRead;
extern DelegateSlot<WriteResult(std::uint16_t) noexcept, 32> ownedProbeWrite;
extern DelegateSlot<CommandResult(std::uint16_t) noexcept, 32> ownedProbeCall;
std::uint16_t readLateCount() noexcept;
struct Count { std::uint16_t operator()() const noexcept { return readLateCount(); } };
extern Count lateCount;
extern constexpr FieldTable lateProbeFields{
    field("Context", "", contextProbeRead), field("Context write", "", lateCount, contextProbeWrite),
    field("Ref", "", refProbeRead), field("Ref write", "", lateCount, refProbeWrite),
    field("Owned", "", ownedProbeRead), field("Owned write", "", lateCount, ownedProbeWrite)};
constexpr FieldCatalogTable lateProbeCatalog{group("slots", lateProbeFields)};
extern constexpr CommandTable lateProbeCommands{
    command("Context", contextProbeCall), command("Ref", refProbeCall), command("Owned", ownedProbeCall)};
constexpr CommandCatalogTable lateProbeActions{group("slots", lateProbeCommands)};
static_assert(sizeof(contextProbeRead) == 8 && sizeof(refProbeRead) == 8);
static_assert(sizeof(ownedProbeRead) == sizeof(tiny::delegate<float(), 32>));
static_assert(sizeof(Field) == 96 && sizeof(Command) == 20);

// Each manual comparison uses the strategy's get() snapshot/view and direct
// invocation; typed table routing must neither box values nor add instructions.
#define LATE_PROBE(Prefix, ReadSlot, WriteSlot, CallSlot, ReadIndex, WriteIndex, CallIndex) \
extern "C" float late_##Prefix##_manual_read() noexcept { \
    auto target = ReadSlot.get(); return target ? target.invoke() : -1.f; } \
extern "C" float late_##Prefix##_local_read() noexcept { return lateProbeFields.read<ReadIndex>().value_or(-1.f); } \
extern "C" float late_##Prefix##_global_read() noexcept { return lateProbeCatalog.read<ReadIndex>().value_or(-1.f); } \
extern "C" WriteResult late_##Prefix##_manual_write(std::uint16_t value) noexcept { \
    auto target = WriteSlot.get(); return target ? target.invoke(value) : WriteResult::Unavailable; } \
extern "C" WriteResult late_##Prefix##_local_write(std::uint16_t value) noexcept { return lateProbeFields.write<WriteIndex>(value); } \
extern "C" WriteResult late_##Prefix##_global_write(std::uint16_t value) noexcept { return lateProbeCatalog.write<WriteIndex>(value); } \
extern "C" CommandResult late_##Prefix##_manual_call(std::uint16_t value) noexcept { \
    auto target = CallSlot.get(); return target ? target.invoke(value) : CommandResult::Unavailable; } \
extern "C" CommandResult late_##Prefix##_local_call(std::uint16_t value) noexcept { return lateProbeCommands.call<CallIndex>(value); } \
extern "C" CommandResult late_##Prefix##_global_call(std::uint16_t value) noexcept { return lateProbeActions.call<CallIndex>(value); }
LATE_PROBE(context, contextProbeRead, contextProbeWrite, contextProbeCall, 0, 1, 0)
LATE_PROBE(ref, refProbeRead, refProbeWrite, refProbeCall, 2, 3, 1)
LATE_PROBE(owned, ownedProbeRead, ownedProbeWrite, ownedProbeCall, 4, 5, 2)
#undef LATE_PROBE
