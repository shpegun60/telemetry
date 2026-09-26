// Cortex-M7 controls for the runtime ID boundary. Wide inputs must check their
// high word; the ordinary uint32_t dispatch remains the existing direct path.
// Authors: Ruslan Kovtun (shpegun60), codexAi. License: MIT.
#include "Telemetry.h"

using namespace telemetry;
float idBoundaryRead() noexcept;
CommandResult idBoundaryCommand() noexcept;

namespace {
constexpr FieldTable rows{field<&idBoundaryRead>("value", "")};
constexpr FieldCatalogTable fields{group("fields", rows)};
constexpr CommandTable commands{command<&idBoundaryCommand>("run")};
constexpr CommandCatalogTable grouped{group("commands", commands)};
}

extern "C" {
CommandResult id_boundary_local_u32(std::uint32_t id) noexcept { return commands.call(id); }
CommandResult id_boundary_local_u64(std::uint64_t id) noexcept { return commands.call(id); }
const Field* id_boundary_field_u32(std::uint32_t id) noexcept { return fields.find(id); }
const Field* id_boundary_field_u64(std::uint64_t id) noexcept { return fields.find(id); }
const Command* id_boundary_command_u32(std::uint32_t id) noexcept { return grouped.find(id); }
const Command* id_boundary_command_u64(std::uint64_t id) noexcept { return grouped.find(id); }
GroupId id_boundary_group_u32(std::uint32_t id) noexcept { return groupOf(id); }
GroupId id_boundary_group_u64(std::uint64_t id) noexcept { return groupOf(id); }
EntryOffset id_boundary_index_u32(std::uint32_t id) noexcept { return indexOf(id); }
EntryOffset id_boundary_index_u64(std::uint64_t id) noexcept { return indexOf(id); }
}
