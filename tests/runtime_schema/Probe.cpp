/*
 * @file Probe.cpp
 * @brief Standalone read-only runtime headers without native reflection or RPC.
 * SPDX-License-Identifier: MIT
 */
#include <telemetry/runtime/DynamicValueView.hpp>

extern "C" telemetry::runtime::Result<std::uint64_t>
runtime_read(const telemetry::runtime::DynamicValueView& view,
             const telemetry::runtime::PreparedHandle& handle) noexcept
{
	return view.get<std::uint64_t>(handle);
}
