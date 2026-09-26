/**
 * @file TelemetryId.h
 * @brief Shared packed, constant-time field and command identifiers.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see ../LICENSE.
 */
#ifndef TELEMETRY_ID_H
#define TELEMETRY_ID_H

#include <cstdint>
#include <cstdlib>
#include <limits>
#include <optional>
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
template <class T>
inline constexpr bool isIdInput = std::is_integral_v<T> || std::is_enum_v<T>;

// Check the original value before converting to a packed ID or a native
// position. On ARM a uint64_t transport value must not wrap through size_t.
template <class To, class From>
constexpr bool indexFits(From value) noexcept
{
    static_assert(std::is_integral_v<To> && std::is_unsigned_v<To>);
    static_assert(isIdInput<From>, "Telemetry identifiers require integers or enums");
    if constexpr (std::is_enum_v<From>) {
        return indexFits<To>(static_cast<std::underlying_type_t<From>>(value));
    } else {
        if constexpr (std::is_signed_v<From>) {
            if (value < 0) return false;
        }
        if constexpr (std::numeric_limits<From>::digits > std::numeric_limits<To>::digits)
            return value <= static_cast<From>(std::numeric_limits<To>::max());
        else return true;
    }
}

// No packed ID is spare: every 32-bit value names a possible position. The
// unchecked-looking makeId API therefore treats invalid components as a
// contract violation instead of manufacturing an ID that could name a row.
[[noreturn]] inline void invalidIdComponent() noexcept { std::abort(); }

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

// The narrow overload remains just a shift and OR. Lookup separately checks
// the actual catalog bounds.
constexpr PackedId makeId(GroupId group, EntryOffset index) noexcept
{
    return (PackedId{group} << 16) | PackedId{index};
}

// Use this form at transport boundaries. An invalid component cannot alias a
// different entry; no conversion takes place until both bounds have passed.
template <class Group, class Position,
          std::enable_if_t<detail::isIdInput<Group> && detail::isIdInput<Position>, int> = 0>
constexpr std::optional<PackedId> tryMakeId(Group group, Position index) noexcept
{
    if (!detail::indexFits<GroupId>(group) || !detail::indexFits<EntryOffset>(index))
        return std::nullopt;
    return makeId(static_cast<GroupId>(group), static_cast<EntryOffset>(index));
}

// Preserve full-width integer literals and variables until validation. Invalid
// constant expressions fail to compile; invalid runtime components terminate.
// For fallible runtime input use tryMakeId() instead.
template <class Group, class Position,
          std::enable_if_t<detail::isIdInput<Group> && detail::isIdInput<Position>, int> = 0>
constexpr PackedId makeId(Group group, Position index) noexcept
{
    const auto id = tryMakeId(group, index);
    return id ? *id : (detail::invalidIdComponent(), PackedId{});
}

// Prevent floating or implicitly-convertible wrapper values from bypassing the
// full-width check by selecting the uint16_t overload.
template <class Group, class Position,
          std::enable_if_t<!detail::isIdInput<Group> || !detail::isIdInput<Position>, int> = 0>
PackedId makeId(Group, Position) = delete;

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
