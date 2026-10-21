/*
 * @file Writer.cpp
 * @brief Stage checked scalar edits and commit a complete encoded value offline.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#include <telemetry/runtime/DynamicValueWriter.hpp>

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

bool save(const char* path, std::span<const std::byte> bytes)
{
	auto* file = std::fopen(path, "wb");
	if (!file)
		return false;
	const bool written = std::fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size();
	return std::fclose(file) == 0 && written;
}
} // namespace

int main(int argc, char** argv)
{
	if (argc != 4)
		return 2;
	namespace rt = telemetry::runtime;
	// All storage belongs to this fixture example. A general application uses
	// its schema budget and checks ParseResult::requirements on Capacity.
	std::array<std::byte, 65536> descriptor{};
	std::array<std::byte, 4096> original{}, scratch{}, output{};
	std::array<rt::RuntimeType, 128> types{};
	std::array<rt::RuntimeMember, 128> members{};
	std::array<rt::RuntimeEnumEntry, 32> enums{};
	std::array<rt::RuntimeCatalog, 16> catalogs{};
	std::array<rt::RuntimeEndpoint, 32> endpoints{};
	rt::RuntimeSchemaWorkspace workspace{types, members, enums, catalogs, endpoints};
	const auto descriptorSize = load(argv[1], descriptor);
	const auto payloadSize = load(argv[2], original);
	const auto parsed = rt::RuntimeSchema::parse({descriptor.data(), descriptorSize}, workspace);
	if (!parsed)
		return 1;
	const auto* field = parsed.schema.findField("dynamic", "Packet");
	if (!field ||
	    !(field->capabilities & std::uint8_t(telemetry::descriptor::Capability::Writable)))
		return 1;
	const auto sample = parsed.schema.resolve(field->typeId, "rows[1].bins[2]");
	const auto mode = parsed.schema.resolve(field->typeId, "wide");
	if (!sample || !mode)
		return 1;
	auto editor = rt::DynamicValueWriter::begin(parsed.schema, field->typeId,
	                                            {original.data(), payloadSize}, scratch);
	if (!editor)
		return 1;
	const std::int8_t replacement = 7;
	const std::uint64_t openEnumCode = 17;
	if (editor.set(sample.value, replacement) != rt::Status::Ok ||
	    editor.setEnumUnderlying(mode.value, openEnumCode) != rt::Status::Ok)
		return 1;
	const auto committed = editor.commit(std::span{output}.first(payloadSize));
	if (!committed)
		return 1;
	const auto updated = parsed.schema.view(field->typeId, {output.data(), committed.value});
	if (!updated)
		return 1;
	const auto value = updated.value.get<std::int8_t>(sample.value);
	const auto enumValue = updated.value.enumUnderlying<std::uint64_t>(mode.value);
	const auto enumName = updated.value.enumName(mode.value);
	if (!value || value.value != replacement || !enumValue || enumValue.value != openEnumCode ||
	    !enumName || enumName.value.known || !save(argv[3], {output.data(), committed.value}))
		return 1;
	std::printf("staged rows[1].bins[2] = %d, unnamed enum = %llu; committed %zu bytes\n",
	            int(value.value), static_cast<unsigned long long>(enumValue.value),
	            committed.value);
	// No transport or device operation is performed by this program.
	return 0;
}
