/* Instantiate optional Workbench read paths for Cortex-M7 without native Model. MIT. */
#include <integration/read_many/Workbench.hpp>
#include <integration/encoded/Backend.hpp>
namespace rm = telemetry::integration::read_many;
namespace en = telemetry::integration::encoded;
using Workbench = telemetry::integration::workbench::WorkbenchModel<4, 130, 64, 768>;

telemetry::client::Submission readManyWorkbenchProbe(Workbench& owner, en::EncodedBackend& backend,
                                                     rm::wire::Capability capability,
                                                     std::span<const std::uint32_t> ids) noexcept
{
	const auto binding = rm::bindReadMany(owner, backend, capability, ids);
	if (!binding)
		return {binding.status, {}};
	return rm::readMany(owner, binding.binding, [](const rm::ReadManyResult&) noexcept {
	});
}

telemetry::client::Submission
readFieldWorkbenchProbe(Workbench& owner,
                        const telemetry::integration::dynamic::EndpointBinding& field) noexcept
{
	return rm::readFieldValue(owner, field, [](const auto&) noexcept {
	});
}
