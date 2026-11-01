/* Instantiate the optional client API for Cortex-M7 without a board or heap. MIT. */
#include <integration/read_many/Client.hpp>
#include <integration/encoded/Backend.hpp>
namespace rm = telemetry::integration::read_many;
namespace en = telemetry::integration::encoded;
using Client = telemetry::integration::dynamic::DynamicClient<4, 130, 640>;

// The application supplies the existing client, backend and admission. This
// root compiles the full bind/submit/completion path without a native Model.
telemetry::client::Submission readManyClientProbe(Client& client, en::EncodedBackend& backend,
                                                  rm::wire::Capability capability,
                                                  std::span<const std::uint32_t> ids) noexcept
{
	const auto binding = rm::bindReadMany(client, backend, capability, ids);
	if (!binding)
		return {binding.status, {}};
	return rm::readMany(client, binding.binding, [](const rm::ReadManyResult&) noexcept {
	});
}
