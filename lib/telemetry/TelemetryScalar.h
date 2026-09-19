#ifndef TELEMETRY_SCALAR_H
#define TELEMETRY_SCALAR_H

#include "TelemetryCompiler.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>
#include <variant>

namespace telemetry {

namespace detail {
template <class T>
inline constexpr bool isScalarNumber = []() constexpr {
    if constexpr (std::is_integral_v<T>) return sizeof(T) <= sizeof(std::uint64_t);
    else return std::is_same_v<T, float> || std::is_same_v<T, double>;
}();

// Read results are values, without reference or cv qualification. Test this
// before forming optional<T>, including for void and incomplete class types.
template <class T>
inline constexpr bool isScalarReadType = isScalarNumber<T> && std::is_same_v<T, std::decay_t<T>>;
} // namespace detail

// Adapted from power_analyzer_h753/app_core/telemetry. The standalone copy
// uses standard C++ types and requires no firmware headers.
static_assert(sizeof(float) == 4 && sizeof(double) == 8,
              "Telemetry requires 32-bit float and 64-bit double");

enum class ScalarType : std::uint8_t {
    Null = 0,
    F32,
    F64,
    U32,
    S32,
    U64,
    Bool,
    // Append types to preserve the numeric codes of existing tags.
    U8,
    U16,
    S8,
    S16,
    S64,
};

// The variant owns both the value and its tag; they cannot be modified
// independently. No alternative allocates or has a throwing copy/move.
struct Scalar {
private:
    // Alternative positions match the stable ScalarType numeric codes.
    using Storage = std::variant<std::monostate, float, double, std::uint32_t,
        std::int32_t, std::uint64_t, bool, std::uint8_t, std::uint16_t,
        std::int8_t, std::int16_t, std::int64_t>;

    Storage storage_{};

    struct NativeTag {};

    template <class T>
    constexpr Scalar(NativeTag, T value) noexcept
        : storage_(std::in_place_type<T>, value) {}

public:
    inline static constexpr std::size_t typeCount = std::variant_size_v<Storage>;
    template <ScalarType Type>
    using NativeType = std::variant_alternative_t<static_cast<std::size_t>(Type), Storage>;

    constexpr Scalar() noexcept = default;

    TELEMETRY_FORCE_INLINE constexpr ScalarType type() const noexcept
    {
        return static_cast<ScalarType>(storage_.index());
    }

    // Checked, exact-type extraction by value. A wrong type follows std::get:
    // bad_variant_access with exceptions enabled; termination without them.
    template <class T, std::enable_if_t<std::is_constructible_v<Storage,
              std::in_place_type_t<T>, T>, int> = 0>
    TELEMETRY_FORCE_INLINE constexpr T get() const { return std::get<T>(storage_); }

    // Non-throwing inspection. The pointer is borrowed until this Scalar is
    // reassigned or destroyed. Extracting it from a temporary is rejected
    // to avoid a dangling result.
    template <class T, std::enable_if_t<std::is_constructible_v<Storage,
              std::in_place_type_t<T>, T>, int> = 0>
    TELEMETRY_FORCE_INLINE constexpr const T* getIf() const & noexcept { return std::get_if<T>(&storage_); }
    template <class T, std::enable_if_t<std::is_constructible_v<Storage,
              std::in_place_type_t<T>, T>, int> = 0>
    const T* getIf() const && = delete;

    // A typed Scalar return can directly return any supported C++ number.
    // Constrain the conversion so pointers/strings cannot enter through bool.
    template <class T, std::enable_if_t<detail::isScalarNumber<T>, int> = 0>
    constexpr Scalar(T value) noexcept : Scalar(from(value)) {}

    static constexpr Scalar null() noexcept { return Scalar(); }
    static constexpr Scalar fromF32(float value) noexcept { return Scalar(NativeTag{}, value); }
    static constexpr Scalar fromF64(double value) noexcept { return Scalar(NativeTag{}, value); }
    static constexpr Scalar fromU8(std::uint8_t value) noexcept { return Scalar(NativeTag{}, value); }
    static constexpr Scalar fromU16(std::uint16_t value) noexcept { return Scalar(NativeTag{}, value); }
    static constexpr Scalar fromU32(std::uint32_t value) noexcept { return Scalar(NativeTag{}, value); }
    static constexpr Scalar fromS8(std::int8_t value) noexcept { return Scalar(NativeTag{}, value); }
    static constexpr Scalar fromS16(std::int16_t value) noexcept { return Scalar(NativeTag{}, value); }
    static constexpr Scalar fromS32(std::int32_t value) noexcept { return Scalar(NativeTag{}, value); }
    static constexpr Scalar fromU64(std::uint64_t value) noexcept { return Scalar(NativeTag{}, value); }
    static constexpr Scalar fromS64(std::int64_t value) noexcept { return Scalar(NativeTag{}, value); }
    static constexpr Scalar fromBool(bool value) noexcept { return Scalar(NativeTag{}, value); }
    static constexpr Scalar from(Scalar value) noexcept { return value; }

    // Preserve the source type when accepting ordinary C++ numbers. Long
    // double, pointers, strings, enums and integers wider than 64 bits need
    // an explicit application conversion before entering the scalar API.
    template <class T, std::enable_if_t<detail::isScalarNumber<T>, int> = 0>
    static constexpr Scalar from(T value) noexcept
    {
        if constexpr (std::is_same_v<T, bool>) return fromBool(value);
        else if constexpr (std::is_same_v<T, float>) return fromF32(value);
        else if constexpr (std::is_same_v<T, double>) return fromF64(value);
        else if constexpr (std::is_signed_v<T>) {
            if constexpr (sizeof(T) <= sizeof(std::int8_t)) return fromS8(static_cast<std::int8_t>(value));
            else if constexpr (sizeof(T) <= sizeof(std::int16_t)) return fromS16(static_cast<std::int16_t>(value));
            else if constexpr (sizeof(T) <= sizeof(std::int32_t)) return fromS32(static_cast<std::int32_t>(value));
            else return fromS64(static_cast<std::int64_t>(value));
        } else {
            if constexpr (sizeof(T) <= sizeof(std::uint8_t)) return fromU8(static_cast<std::uint8_t>(value));
            else if constexpr (sizeof(T) <= sizeof(std::uint16_t)) return fromU16(static_cast<std::uint16_t>(value));
            else if constexpr (sizeof(T) <= sizeof(std::uint32_t)) return fromU32(static_cast<std::uint32_t>(value));
            else return fromU64(static_cast<std::uint64_t>(value));
        }
    }

private:
    static_assert(std::is_trivially_copyable_v<Storage>
                  && std::is_nothrow_copy_constructible_v<Storage>
                  && std::is_nothrow_move_constructible_v<Storage>
                  && std::is_nothrow_copy_assignable_v<Storage>
                  && std::is_nothrow_move_assignable_v<Storage>,
                  "Scalar storage must not become valueless during assignment");
};

} // namespace telemetry

#endif
