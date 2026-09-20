/**
 * @file TelemetryId.h
 * @brief Shared packed, constant-time field and command identifiers.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see ../LICENSE.
 */
#ifndef TELEMETRY_ID_H
#define TELEMETRY_ID_H

#include <cstdint>
#include <type_traits>

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

namespace detail {
// Preserve a template position at full width until the owning table checks
// its actual size. Casting an enum straight to size_t could wrap a U64 code
// to a valid row on ARM32. This helper is evaluated only at compile time.
template <auto Position>
constexpr std::uintmax_t positionValue() noexcept
{
    using T = std::remove_cv_t<decltype(Position)>;
    static_assert(std::is_integral_v<T> || std::is_enum_v<T>,
                  "Typed table position must be an integer or enum");
    if constexpr (std::is_enum_v<T>) {
        return positionValue<static_cast<std::underlying_type_t<T>>(Position)>();
    } else if constexpr (std::is_integral_v<T>) {
        static_assert(sizeof(T) <= sizeof(std::uintmax_t),
                      "Typed table position is wider than uintmax_t");
        if constexpr (std::is_signed_v<T>)
            static_assert(Position >= 0, "Typed table position must be non-negative");
        return static_cast<std::uintmax_t>(Position);
    } else return 0; // Keep invalid types out of subsequent casts/instantiations.
}
} // namespace detail

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
