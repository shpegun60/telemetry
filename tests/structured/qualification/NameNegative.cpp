/* A typed null name remains rejected even in the GCC null-check mode.
 * Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT. */
#include <telemetry/Telemetry.hpp>

std::uint32_t readValue() noexcept
{
	return 1;
}

inline constexpr const char* missingName = nullptr;
inline constexpr auto rejected = telemetry::field<&readValue>(missingName);
