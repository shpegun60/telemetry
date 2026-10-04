/* Compile-time growth with recursive reflected structs, without changing core.
 * Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT. */
#include <telemetry/Telemetry.hpp>
#include <resource/telemetry/v3/Descriptor.hpp>

namespace ts = telemetry;

template<unsigned D>
struct Nested {
	Nested<D - 1> child;
};

template<>
struct Nested<0> {
	std::uint32_t value;
};

Nested<DEPTH> readValue() noexcept
{
	return {};
}

inline constexpr ts::FieldTable table{ts::field<&readValue>("nested")};
inline constexpr ts::FieldCatalogTable fields{ts::group("depth", table)};
inline constexpr ts::CommandCatalogTable<> commands{};
inline constexpr ts::ServiceCatalogTable<> services{};
inline constexpr ts::Model model{fields, commands, services};
inline constexpr resource::telemetry::v3::Descriptor descriptor{model};
inline constexpr auto bytes = resource::telemetry::v3::packDescriptor<descriptor>();
static_assert(ts::wireSize<Nested<DEPTH>> == 4);

extern "C" unsigned depth_byte(unsigned index) noexcept
{
	return index < bytes.size() ? std::to_integer<unsigned>(bytes[index]) : 0;
}
