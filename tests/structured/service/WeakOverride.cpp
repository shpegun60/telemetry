/*
 * @file WeakOverride.cpp
 * @brief Strong replacement for the Stage 06 weak Service target.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */

#include <cstdint>

struct WeakRequest { std::uint8_t id; };
struct WeakResponse { std::uint8_t value; };

struct WeakDevice {
    WeakResponse absent(const WeakRequest&) const noexcept __attribute__((weak));
    WeakResponse replaceable(const WeakRequest&) const noexcept;
};

extern "C" WeakResponse replaceable_service(const WeakRequest&) noexcept
{
    return {22};
}

WeakResponse WeakDevice::replaceable(const WeakRequest&) const noexcept
{
    return {44};
}
