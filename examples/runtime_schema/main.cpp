/*
 * @file main.cpp
 * @brief Read one borrowed value using a runtime descriptor and a prepared path.
 * SPDX-License-Identifier: MIT
 */
#include <telemetry/runtime/DynamicValueView.hpp>

#include <array>
#include <cstdio>

namespace {
template<std::size_t N>
std::size_t load(const char* path, std::array<std::byte, N>& bytes)
{
	auto* file = std::fopen(path, "rb");
	if (!file)
		return 0;
	const auto count = std::fread(bytes.data(), 1, bytes.size(), file);
	const bool exact = std::fgetc(file) == EOF;
	std::fclose(file);
	return exact ? count : 0;
}
} // namespace

int main(int argc, char** argv)
{
	if (argc != 3)
		return 2;
	namespace rt = telemetry::runtime;
	// These capacities belong to this example's known fixture, not every peer.
	// A general application retries parse with the reported Requirements using
	// storage that remains alive until all schemas, handles and views are gone.
	std::array<std::byte, 65536> descriptor{};
	std::array<std::byte, 4096> payload{};
	std::array<rt::RuntimeType, 128> types{};
	std::array<rt::RuntimeMember, 128> members{};
	std::array<rt::RuntimeEnumEntry, 32> enums{};
	std::array<rt::RuntimeCatalog, 16> catalogs{};
	std::array<rt::RuntimeEndpoint, 32> endpoints{};
	rt::RuntimeSchemaWorkspace workspace{types, members, enums, catalogs, endpoints};
	const auto descriptorSize = load(argv[1], descriptor);
	const auto valueSize = load(argv[2], payload);
	const auto parsed = rt::RuntimeSchema::parse({descriptor.data(), descriptorSize}, workspace);
	if (!parsed)
		return 1;
	const auto* endpoint = parsed.schema.findField("dynamic", "Packet");
	if (!endpoint)
		return 1;
	const auto path = parsed.schema.resolve(endpoint->typeId, "rows[1].bins[2]");
	if (!path)
		return 1;
	const auto view = parsed.schema.view(endpoint->typeId, {payload.data(), valueSize});
	if (!view)
		return 1;
	const auto result = view.value.get<std::int8_t>(path.value);
	if (!result)
		return 1;
	std::printf("rows[1].bins[2] = %d\n", int(result.value));
	return result.value == -3 ? 0 : 1;
}
