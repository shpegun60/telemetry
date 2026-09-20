/**
 * @file TelemetryId.h
 * @brief Shared packed, constant-time field and command identifiers.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see ../LICENSE.
 */
#ifndef TELEMETRY_ID_H
#define TELEMETRY_ID_H

#include <cstdint>

namespace telemetry {

// Packed identity: high 16 bits are the zero-based group position; low
// 16 bits are the zero-based entry position within that group. Fields and
// commands use separate logical spaces with the same representation.
using PackedId = std::uint32_t;
using FieldId = PackedId;
using CommandId = PackedId;
using GroupId = std::uint16_t;
using EntryOffset = std::uint16_t;
using FieldOffset = EntryOffset;
using CommandOffset = EntryOffset;
inline constexpr std::uint32_t idComponentCapacity = 65536u;

// Components are already 16-bit positions. Validate a wider transport value
// before narrowing it to these parameter types; packing does not validate a
// catalog's actual size (the index performs those bounds checks).
constexpr PackedId makeId(GroupId group, EntryOffset index) noexcept
{
    return (PackedId{group} << 16) | PackedId{index};
}

constexpr GroupId groupOf(PackedId id) noexcept
{
    return static_cast<GroupId>(id >> 16);
}

constexpr EntryOffset indexOf(PackedId id) noexcept
{
    return static_cast<EntryOffset>(id & 0xffffu);
}

} // namespace telemetry

#endif
