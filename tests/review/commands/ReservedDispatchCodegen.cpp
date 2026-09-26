// Review probe (commands slice), compile-only for Cortex-M7: README says the
// runtime-position dispatcher "emits comparisons/invocations only for
// definitions with the supplied argument count". Reserved rows accept every
// arity (ReservedCommandDefinition::acceptsArguments is always true), so each
// one adds a branch to every runtime-dispatch instantiation.
#include "command/TelemetryCommandTable.h"

enum class ReservedProbeMode : std::uint16_t { Off, On };
struct ReservedProbeOwner {
    telemetry::CommandResult pair(float, ReservedProbeMode) noexcept;
    telemetry::CommandResult one(std::uint16_t) noexcept;
};
extern ReservedProbeOwner reservedProbeOwner;

namespace {
constexpr telemetry::CommandTable plain{
    telemetry::command<&ReservedProbeOwner::one>("a", reservedProbeOwner),
    telemetry::command<&ReservedProbeOwner::pair>("b", reservedProbeOwner),
    telemetry::command<&ReservedProbeOwner::one>("c", reservedProbeOwner),
    telemetry::command<&ReservedProbeOwner::one>("d", reservedProbeOwner),
    telemetry::command<&ReservedProbeOwner::one>("e", reservedProbeOwner)};
constexpr telemetry::CommandTable withReserved{
    telemetry::reservedCommand(),
    telemetry::command<&ReservedProbeOwner::pair>("b", reservedProbeOwner),
    telemetry::reservedCommand(),
    telemetry::command<&ReservedProbeOwner::one>("d", reservedProbeOwner),
    telemetry::reservedCommand()};
} // namespace

extern "C" __attribute__((noinline)) telemetry::CommandResult reserved_probe_plain(
    std::size_t index, float value, ReservedProbeMode mode) noexcept
{ return plain.call(index, value, mode); }
extern "C" __attribute__((noinline)) telemetry::CommandResult reserved_probe_reserved(
    std::size_t index, float value, ReservedProbeMode mode) noexcept
{ return withReserved.call(index, value, mode); }
