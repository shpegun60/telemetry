/* Immutable resource provider in a separate TU. Authors: Ruslan Kovtun
 * (shpegun60), codexAi. SPDX-License-Identifier: MIT. */
#include "Fixture.hpp"

namespace qualification {
inline constexpr rs::Descriptor descriptor{fixture::model};
inline constexpr auto packed = rs::packDescriptor<descriptor>();
inline constexpr rs::ValuesFile values{descriptor, sharedWorkspace};

ts::ModelView modelView() noexcept
{
	return fixture::model.view();
}

std::span<const std::byte> descriptorBytes() noexcept
{
	return packed;
}

std::uint64_t fingerprint() noexcept
{
	return descriptor.fingerprint();
}

resource::ReadResult readValues(resource::Cursor cursor, std::span<std::byte> output) noexcept
{
	return values.read(cursor, output);
}
} // namespace qualification
