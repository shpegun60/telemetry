/**
 * @file BinaryScalar.hpp
 * @brief Exact scalar bits and explicitly little-endian stores, including unaligned output.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see ../../LICENSE.
 */
#pragma once
#include "../BinaryFormat.hpp"
#include <bit>
#include <cstddef>
#include <limits>
#include <type_traits>
#include <variant>

namespace telemetry_resource::detail
{
static_assert(sizeof(float) == 4 && sizeof(double) == 8 && std::numeric_limits<float>::is_iec559 &&
              std::numeric_limits<double>::is_iec559);

inline std::uint64_t scalarBits(const telemetry::Scalar& value) noexcept
{
    return value.visit(
        [](auto number) noexcept -> std::uint64_t
        {
            using T = decltype(number);
            if constexpr (std::is_same_v<T, std::monostate>)
            {
                return 0;
            }
            else if constexpr (std::is_same_v<T, float>)
            {
                return std::bit_cast<std::uint32_t>(number);
            }
            else if constexpr (std::is_same_v<T, double>)
            {
                return std::bit_cast<std::uint64_t>(number);
            }
            // Conversion to unsigned is defined modulo 2^64, including INT64_MIN.
            else
            {
                return static_cast<std::uint64_t>(number);
            }
        });
}

// The caller reserves width bytes first. Work with 32-bit words so narrow
// payloads do not introduce variable 64-bit shifts on Cortex-M.
inline void storePayload(std::byte* out, std::uint64_t bits, unsigned width) noexcept
{
    auto low = static_cast<std::uint32_t>(bits);
    for (unsigned i = 0; i < width && i < 4; ++i)
    {
        out[i] = static_cast<std::byte>(low & 0xffu);
        low >>= 8;
    }
    if (width == 8)
    {
        auto high = static_cast<std::uint32_t>(bits >> 32);
        for (unsigned i = 4; i < 8; ++i)
        {
            out[i] = static_cast<std::byte>(high & 0xffu);
            high >>= 8;
        }
    }
}
} // namespace telemetry_resource::detail
