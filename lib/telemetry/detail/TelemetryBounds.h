/**
 * @file TelemetryBounds.h
 * @brief Internal fixed-size storage for telemetry numeric bounds.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see ../LICENSE.
 *
 * This header is an implementation detail and is not a stable public API.
 */
#ifndef TELEMETRY_DETAIL_BOUNDS_H
#define TELEMETRY_DETAIL_BOUNDS_H

#include <cstdint>
#include <limits>
#include <type_traits>

namespace telemetry {
namespace detail {

template <class T>
struct NumericBounds {
    T minimum = std::numeric_limits<T>::lowest();
    T maximum = std::numeric_limits<T>::max();
};

// FieldType selects the active member and retains the corresponding ScalarType.
// Every alternative is trivial, so whole-object copying preserves the payload.
// Never inspect a different alternative to reinterpret the same bytes. Even
// equal-sized alternatives require selecting the member named by the type tag.
union FieldBounds {
    NumericBounds<float> f32;
    NumericBounds<double> f64;
    NumericBounds<std::uint32_t> u32;
    NumericBounds<std::int32_t> s32;
    NumericBounds<std::uint64_t> u64;
    NumericBounds<bool> boolean;
    NumericBounds<std::uint8_t> u8;
    NumericBounds<std::uint16_t> u16;
    NumericBounds<std::int8_t> s8;
    NumericBounds<std::int16_t> s16;
    NumericBounds<std::int64_t> s64;

    constexpr FieldBounds() noexcept : f32{} {}
    constexpr explicit FieldBounds(NumericBounds<float> value) noexcept : f32(value) {}
    constexpr explicit FieldBounds(NumericBounds<double> value) noexcept : f64(value) {}
    constexpr explicit FieldBounds(NumericBounds<std::uint32_t> value) noexcept : u32(value) {}
    constexpr explicit FieldBounds(NumericBounds<std::int32_t> value) noexcept : s32(value) {}
    constexpr explicit FieldBounds(NumericBounds<std::uint64_t> value) noexcept : u64(value) {}
    constexpr explicit FieldBounds(NumericBounds<bool> value) noexcept : boolean(value) {}
    constexpr explicit FieldBounds(NumericBounds<std::uint8_t> value) noexcept : u8(value) {}
    constexpr explicit FieldBounds(NumericBounds<std::uint16_t> value) noexcept : u16(value) {}
    constexpr explicit FieldBounds(NumericBounds<std::int8_t> value) noexcept : s8(value) {}
    constexpr explicit FieldBounds(NumericBounds<std::int16_t> value) noexcept : s16(value) {}
    constexpr explicit FieldBounds(NumericBounds<std::int64_t> value) noexcept : s64(value) {}
};

static_assert(sizeof(FieldBounds) == 16 && std::is_trivially_copyable_v<FieldBounds>);

} // namespace detail
} // namespace telemetry

#endif
