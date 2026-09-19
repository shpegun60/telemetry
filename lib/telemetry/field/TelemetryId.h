/**
 * @file TelemetryId.h
 * @brief Packed, constant-time telemetry field identifiers.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see ../LICENSE.
 */
#ifndef TELEMETRY_FIELD_ID_H
#define TELEMETRY_FIELD_ID_H

#include <cstdint>

namespace telemetry {

// Packed identity: high 16 bits are the zero-based group position; low
// 16 bits are the zero-based field position within that group.
using FieldId = std::uint32_t;
using GroupId = std::uint16_t;
using FieldOffset = std::uint16_t;
inline constexpr std::uint32_t idComponentCapacity = 65536u;

constexpr FieldId makeId(GroupId group, FieldOffset index) noexcept
{
    return (FieldId{group} << 16) | FieldId{index};
}

constexpr GroupId groupOf(FieldId id) noexcept
{
    return static_cast<GroupId>(id >> 16);
}

constexpr FieldOffset indexOf(FieldId id) noexcept
{
    return static_cast<FieldOffset>(id & 0xffffu);
}

} // namespace telemetry

#endif
