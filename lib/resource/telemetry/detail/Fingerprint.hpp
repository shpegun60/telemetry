/**
 * @file Fingerprint.hpp
 * @brief FNV-1a 64 semantic fingerprint; not a payload integrity check.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see ../../LICENSE.
 */
#pragma once
#include <telemetry/core/TelemetryCompiler.h>
#include <cstddef>
#include <cstdint>
#include <span>

namespace telemetry_resource::detail
{
class Fingerprint
{
public:
    // Two words keep cached fingerprints four-byte aligned on ARM32. Arithmetic
    // remains ordinary unsigned FNV-1a 64, with overflow modulo 2^64.
    constexpr explicit Fingerprint(std::uint64_t initial = UINT64_C(0xcbf29ce484222325)) noexcept
        : low_(static_cast<std::uint32_t>(initial)),
          high_(static_cast<std::uint32_t>(initial >> 32))
    {
    }

    // Used only during metadata construction. Keep the wider hash loop out of
    // every inlined BinaryWriter primitive, including the metadata read code.
    TELEMETRY_NOINLINE void bytes(std::span<const std::byte> bytes) noexcept
    {
        auto hash = value();
        for (const auto byte : bytes)
        {
            hash = (hash ^ std::to_integer<std::uint8_t>(byte)) * UINT64_C(0x100000001b3);
        }
        low_ = static_cast<std::uint32_t>(hash);
        high_ = static_cast<std::uint32_t>(hash >> 32);
    }

    constexpr std::uint64_t value() const noexcept
    {
        return (std::uint64_t{high_} << 32) | low_;
    }

private:
    std::uint32_t low_;
    std::uint32_t high_;
};
} // namespace telemetry_resource::detail
