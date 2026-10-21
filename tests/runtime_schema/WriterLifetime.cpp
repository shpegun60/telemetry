/*
 * @file WriterLifetime.cpp
 * @brief Editor ownership moves and invalidation before backing storage is freed.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#include <telemetry/runtime/DynamicValueWriter.hpp>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

namespace {
namespace rt = telemetry::runtime;
unsigned checks = 0;

void check(bool ok, int line)
{
	++checks;
	if (!ok) {
		std::fprintf(stderr, "writer lifetime check %u line %d failed\n", checks, line);
		std::exit(1);
	}
}

#define CHECK(...) check(bool(__VA_ARGS__), __LINE__)

struct Storage {
	std::unique_ptr<rt::RuntimeType[]> types{new rt::RuntimeType[128]{}};
	std::unique_ptr<rt::RuntimeMember[]> members{new rt::RuntimeMember[128]{}};
	std::unique_ptr<rt::RuntimeEnumEntry[]> enums{new rt::RuntimeEnumEntry[32]{}};
	std::unique_ptr<rt::RuntimeCatalog[]> catalogs{new rt::RuntimeCatalog[16]{}};
	std::unique_ptr<rt::RuntimeEndpoint[]> endpoints{new rt::RuntimeEndpoint[32]{}};
	rt::RuntimeSchemaWorkspace workspace{{types.get(), 128},
	                                     {members.get(), 128},
	                                     {enums.get(), 32},
	                                     {catalogs.get(), 16},
	                                     {endpoints.get(), 32}};

	void releaseMetadata()
	{
		types.reset();
		members.reset();
		enums.reset();
		catalogs.reset();
		endpoints.reset();
	}
};

std::vector<std::byte> load(const char* name)
{
	std::ifstream file(name, std::ios::binary);
	CHECK(file.good());
	const std::vector<char> chars{std::istreambuf_iterator<char>{file},
	                              std::istreambuf_iterator<char>{}};
	std::vector<std::byte> bytes(chars.size());
	for (std::size_t i = 0; i < chars.size(); ++i)
		bytes[i] = std::byte(static_cast<unsigned char>(chars[i]));
	return bytes;
}

rt::PreparedHandle leaf(const rt::RuntimeSchema& schema)
{
	const auto* field = schema.findField("dynamic", "Packet");
	CHECK(field);
	const auto result = schema.resolve(field->typeId, "u64");
	CHECK(result);
	return result.value;
}

void sourceAndMoves(const std::vector<std::byte>& descriptor, const std::vector<std::byte>& payload)
{
	static_assert(!std::is_copy_constructible_v<rt::DynamicValueWriter>);
	static_assert(!std::is_copy_assignable_v<rt::DynamicValueWriter>);
	static_assert(std::is_nothrow_move_constructible_v<rt::DynamicValueWriter>);
	static_assert(std::is_nothrow_move_assignable_v<rt::DynamicValueWriter>);
	Storage storage;
	// The parse result temporary is destroyed before the writer is created.
	auto schema = rt::RuntimeSchema::parse(descriptor, storage.workspace).schema;
	CHECK(schema.valid());
	const auto handle = leaf(schema);
	auto original = std::make_unique<std::vector<std::byte>>(payload);
	std::vector<std::byte> staging(payload.size() + 7, std::byte{0xa5});
	std::vector<std::byte> abandoned(payload.size(), std::byte{0x7b});
	std::vector<std::byte> output(payload.size(), std::byte{0xcc});
	auto expected = payload;
	const auto edited = UINT64_C(0x1122334455667788);
	for (unsigned i = 0; i < 8; ++i)
		expected[handle.wireOffset() + i] = std::byte((edited >> (8 * i)) & 0xffu);
	// Exercise move construction with the source editor object then destroyed.
	auto writer = [&] {
		auto first = rt::DynamicValueWriter::begin(schema, handle.rootType(), *original, staging);
		CHECK(first);
		auto moved = std::move(first);
		CHECK(moved);
		CHECK(first.status() == rt::Status::Stale);
		CHECK(first.set(handle, edited) == rt::Status::Stale);
		CHECK(first.commit(output).status == rt::Status::Stale);
		CHECK(std::ranges::all_of(output, [](auto b) {
			return b == std::byte{0xcc};
		}));
		return moved;
	}();
	// Move assignment abandons only the old lease; neither buffer is committed.
	auto assigned = rt::DynamicValueWriter::begin(schema, handle.rootType(), *original, abandoned);
	CHECK(assigned);
	const auto abandonedBefore = abandoned;
	assigned = std::move(writer);
	CHECK(assigned);
	CHECK(writer.status() == rt::Status::Stale);
	CHECK(abandoned == abandonedBefore);
	// A writer must retain neither the original schema object nor source bytes.
	schema = {};
	original.reset();
	CHECK(assigned.set(handle, edited) == rt::Status::Ok);
	const auto committed = assigned.commit(output);
	CHECK(committed);
	CHECK(committed.value == payload.size());
	CHECK(output == expected);
	CHECK(std::ranges::all_of(std::span{staging}.subspan(payload.size()), [](auto b) {
		return b == std::byte{0xa5};
	}));
	CHECK(assigned.status() == rt::Status::Stale);
	const auto final = output;
	CHECK(assigned.set(handle, edited) == rt::Status::Stale);
	CHECK(assigned.commit(output).status == rt::Status::Stale);
	CHECK(output == final);
}

void staleAfterRelease(const std::vector<std::byte>& descriptor,
                       const std::vector<std::byte>& payload)
{
	for (unsigned mode = 0; mode < 3; ++mode) {
		Storage storage;
		auto bytes = std::make_unique<std::vector<std::byte>>(descriptor);
		auto original = std::make_unique<std::vector<std::byte>>(payload);
		auto scratch = std::make_unique<std::vector<std::byte>>(payload.size());
		const auto schema = rt::RuntimeSchema::parse(*bytes, storage.workspace).schema;
		CHECK(schema.valid());
		const auto handle = leaf(schema);
		auto writer = rt::DynamicValueWriter::begin(schema, handle.rootType(), *original, *scratch);
		CHECK(writer);
		if (mode == 0) {
			CHECK(storage.workspace.reset() == rt::Status::Ok);
		} else if (mode == 1) {
			const std::array<std::byte, 63> shortDescriptor{};
			CHECK(rt::RuntimeSchema::parse(shortDescriptor, storage.workspace).status ==
			      rt::Status::InvalidDescriptor);
		} else {
			const auto* alias = reinterpret_cast<const std::byte*>(storage.types.get());
			CHECK(rt::RuntimeSchema::parse({alias, descriptor.size()}, storage.workspace).status ==
			      rt::Status::Overlap);
		}
		// Every borrowed allocation is now actually freed. Only the workspace
		// control object survives; stale status checks must precede every access.
		original.reset();
		scratch.reset();
		bytes.reset();
		storage.releaseMetadata();
		std::vector<std::byte> output(payload.size(), std::byte{0x5a});
		const auto before = output;
		CHECK(!writer);
		CHECK(writer.status() == rt::Status::Stale);
		auto moved = std::move(writer);
		CHECK(moved.status() == rt::Status::Stale);
		rt::DynamicValueWriter destination;
		// Moving a stale editor must not inspect its former scratch or indexes.
		destination = std::move(moved);
		CHECK(moved.status() == rt::Status::Stale);
		CHECK(destination.set(handle, UINT64_C(7)) == rt::Status::Stale);
		CHECK(destination.setEnumUnderlying(handle, UINT64_C(7)) == rt::Status::Stale);
		CHECK(destination.commit(output).status == rt::Status::Stale);
		CHECK(destination.commit(output).value == 0);
		CHECK(output == before);
	}
}

void stickyAfterRelease(const std::vector<std::byte>& descriptor,
                        const std::vector<std::byte>& payload)
{
	for (unsigned failure = 0; failure < 3; ++failure) {
		Storage storage;
		auto bytes = std::make_unique<std::vector<std::byte>>(descriptor);
		auto scratch = std::make_unique<std::vector<std::byte>>(payload.size());
		const auto schema = rt::RuntimeSchema::parse(*bytes, storage.workspace).schema;
		CHECK(schema.valid());
		const auto handle = leaf(schema);
		auto writer = rt::DynamicValueWriter::begin(schema, handle.rootType(), payload, *scratch);
		CHECK(writer);
		const auto stagingBefore = *scratch;
		auto expected = rt::Status::TypeMismatch;
		if (failure == 0) {
			CHECK(writer.set(handle, std::int64_t{-9}) == expected);
		} else if (failure == 1) {
			expected = rt::Status::InvalidPayload;
			std::array<std::byte, 1> shortOutput{std::byte{0xe3}};
			CHECK(writer.commit(shortOutput).status == expected);
			CHECK(shortOutput[0] == std::byte{0xe3});
		} else {
			expected = rt::Status::Overlap;
			CHECK(writer.commit(*scratch).status == expected);
		}
		CHECK(*scratch == stagingBefore);
		CHECK(writer.status() == expected);
		CHECK(storage.workspace.reset() == rt::Status::Ok);
		bytes.reset();
		scratch.reset();
		storage.releaseMetadata();
		std::vector<std::byte> output(payload.size(), std::byte{0xd4});
		const auto before = output;
		auto moved = std::move(writer);
		CHECK(moved.status() == expected);
		CHECK(moved.set(handle, UINT64_C(8)) == expected);
		CHECK(moved.setEnumUnderlying(handle, UINT64_C(8)) == expected);
		CHECK(moved.commit(output).status == expected);
		CHECK(moved.commit(output).value == 0);
		CHECK(output == before);
	}
}
} // namespace

int main(int argc, char** argv)
{
	if (argc != 3)
		return 2;
	const auto descriptor = load(argv[1]);
	const auto payload = load(argv[2]);
	sourceAndMoves(descriptor, payload);
	staleAfterRelease(descriptor, payload);
	stickyAfterRelease(descriptor, payload);
	std::printf("writer lifetime: %u checks, 0 failures\n", checks);
}
