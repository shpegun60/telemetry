/*
 * Private aggregation implementation publishing only borrowed runtime views.
 *
 * Tables/catalogs/Model and the file table are constant-initialized. Descriptor
 * construction needs values of extern module rows, so the first fingerprint
 * query caches it automatically rather than claiming a constexpr packed schema.
 * Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
 */
#include "Composition.hpp"
#include "Runtime.hpp"
#include <resource/FileSystem.hpp>
#include <resource/telemetry/v3/Descriptor.hpp>
#include <cstdint>
#include <cstdlib>

namespace modular {
namespace {
constinit const auto view = composition::model.view();
constinit const auto fileTable =
    resource::filesystem(resource::file("/module/version.bin", moduleA::version));

const auto& descriptor() noexcept
{
	static const resource::telemetry::v3::Descriptor value{composition::model};
	if (!value.valid()) {
		std::abort();
	}
	return value;
}
} // namespace

const telemetry::ModelView& modelView() noexcept
{
	return view;
}

resource::FileSystemView files() noexcept
{
	return fileTable.view();
}

std::uint64_t fingerprint() noexcept
{
	return descriptor().fingerprint();
}

std::uint32_t typeCount() noexcept
{
	return composition::Registry::typeCount;
}
} // namespace modular
