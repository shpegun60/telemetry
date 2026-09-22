/**
 * @file Fingerprint.hpp
 * @brief Byte-defined FNV-1a semantic fingerprint; not a payload integrity check.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see ../../LICENSE.
 */
#pragma once
#include <cstddef>
#include <cstdint>
#include <span>

namespace telemetry_resource::detail
{
class Fingerprint
{
public:
    void bytes(std::span<const std::byte> bytes) noexcept
    {
        for (const auto byte : bytes)
        {
            value_ = (value_ ^ std::to_integer<std::uint8_t>(byte)) * 16777619u;
        }
    }

    std::uint32_t value() const noexcept
    {
        return value_;
    }

private:
    std::uint32_t value_ = 2166136261u;
};
} // namespace telemetry_resource::detail
