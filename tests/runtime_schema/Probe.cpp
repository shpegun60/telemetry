/*
 * @file Probe.cpp
 * @brief Standalone runtime value headers without native reflection or RPC.
 * SPDX-License-Identifier: MIT
 */
#include <telemetry/runtime/DynamicValueWriter.hpp>

extern "C" telemetry::runtime::Result<std::uint64_t>
runtime_read(const telemetry::runtime::DynamicValueView& view,
             const telemetry::runtime::PreparedHandle& handle) noexcept
{
	return view.get<std::uint64_t>(handle);
}

extern "C" telemetry::runtime::Status
runtime_write(telemetry::runtime::DynamicValueWriter& writer,
              const telemetry::runtime::PreparedHandle& handle, const double& value) noexcept
{
	return writer.set(handle, value);
}
