/**
 * @file TelemetryFieldFlags.h
 * @brief Compact field policy metadata, independent of read/write capability.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see ../LICENSE.
 */
#ifndef TELEMETRY_FIELD_FLAGS_H
#define TELEMETRY_FIELD_FLAGS_H

#include <cstdint>
#include <type_traits>

namespace telemetry {
// Stable wire bits. A consumer ignores unknown bits; existing bits are never
// renumbered. Persistent selects save + restore, not an automatic storage backend.
enum class FieldFlag : std::uint32_t {
    None = 0,
    Persistent = 1u << 0,
};

class FieldFlags {
public:
    constexpr FieldFlags() noexcept = default;
    constexpr FieldFlags(FieldFlag flag) noexcept : value_(static_cast<std::uint32_t>(flag)) {}

    // Unknown bits are preserved for forward-compatible schema consumers.
    // Ordinary integers cannot enter a field's policy by implicit conversion.
    static constexpr FieldFlags fromRaw(std::uint32_t value) noexcept
    {
        FieldFlags result;
        result.value_ = value;
        return result;
    }
    constexpr std::uint32_t value() const noexcept { return value_; }
    constexpr bool empty() const noexcept { return value_ == 0; }
    // All requested bits must be present. The empty mask is always contained.
    constexpr bool contains(FieldFlags requested) const noexcept
    { return (value_ & requested.value_) == requested.value_; }

private:
    std::uint32_t value_ = 0;
};

constexpr FieldFlags operator|(FieldFlags left, FieldFlags right) noexcept
{ return FieldFlags::fromRaw(left.value() | right.value()); }
constexpr FieldFlags operator|(FieldFlag left, FieldFlag right) noexcept
{ return FieldFlags{left} | FieldFlags{right}; }
constexpr bool operator==(FieldFlags left, FieldFlags right) noexcept
{ return left.value() == right.value(); }
constexpr bool operator!=(FieldFlags left, FieldFlags right) noexcept
{ return !(left == right); }

static_assert(sizeof(FieldFlags) == sizeof(std::uint32_t)
              && alignof(FieldFlags) == alignof(std::uint32_t)
              && std::is_trivially_copyable_v<FieldFlags>);
} // namespace telemetry
#endif
