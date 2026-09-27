/*
 * @file CodecNegative.cpp
 * @brief Reject a pathological DMI initializer expansion at compile time.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */

#include <telemetry_structured/codec/Codec.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

struct Defaulted {
    std::uint8_t value = 1;
};

using Many = std::array<Defaulted, 1024>;

void invalidExpansion(std::span<const std::byte> input,
                      telemetry::structured::Workspace& workspace)
{
    auto lease = workspace.reserve<Many>();
    Many* output = nullptr;
    (void)telemetry::structured::decode(input, lease, output);
}
