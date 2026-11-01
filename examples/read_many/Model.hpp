/* Local peer for the optional Encoded ReadMany example. MIT. */
#ifndef TELEMETRY_EXAMPLE_READ_MANY_MODEL_HPP
#define TELEMETRY_EXAMPLE_READ_MANY_MODEL_HPP
#pragma once
#include <telemetry/Telemetry.hpp>
#include <telemetry/descriptor/Descriptor.hpp>

namespace peer {
struct Device {
	std::uint32_t voltage() const noexcept
	{
		return 230100;
	}

	std::uint64_t energy() const noexcept
	{
		return UINT64_MAX;
	}

	bool enabled() const noexcept
	{
		return true;
	}
};

inline Device device;
inline constexpr telemetry::FieldTable measures{
    telemetry::field<&Device::voltage>("Voltage_mV", device),
    telemetry::field<&Device::energy>("Energy", device)};
inline constexpr telemetry::FieldTable controls{
    telemetry::field<&Device::enabled>("Enabled", device)};
inline constexpr telemetry::FieldCatalogTable fields{telemetry::group("measure", measures),
                                                     telemetry::group("control", controls)};
inline constexpr telemetry::ServiceCatalogTable<> services{};
inline constexpr telemetry::Model model{fields, telemetry::emptyCommands, services};
inline constexpr telemetry::descriptor::Descriptor descriptor{model};
inline constexpr auto descriptorBytes = telemetry::descriptor::packDescriptor<descriptor>();
} // namespace peer
#endif // TELEMETRY_EXAMPLE_READ_MANY_MODEL_HPP
