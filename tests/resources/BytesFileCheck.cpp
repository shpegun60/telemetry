// Borrowed byte resources: cursor, overlap, lifetime guards and composition.
// Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
// Checks bounded cursor access to borrowed byte arrays and spans.
// Canaries, overlapping backing ranges and large cursor values expose extent or copying mistakes.

#include <resource/Resource.hpp>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <type_traits>
#include <utility>

namespace {
constexpr std::array source{std::byte{10}, std::byte{20}, std::byte{30}, std::byte{40},
                            std::byte{50}};
constexpr resource::BytesFile file{source};
constexpr resource::BytesFile empty{resource::Input{}};
constexpr auto files = resource::filesystem(resource::file("/data/raw.bin", file),
                                            resource::file("/empty.bin", empty));
static_assert(file.size() == source.size() && empty.size() == 0);
static_assert(files.fileCount() == 2 && files.path(0) == "/data/raw.bin");
static_assert(resource::ReadableProvider<resource::BytesFile>);
static_assert(!resource::WritableProvider<resource::BytesFile>);
static_assert(std::is_trivially_copyable_v<resource::BytesFile>);
static_assert(sizeof(resource::BytesFile) == sizeof(resource::Input));

using Array = std::array<std::byte, 5>;
using CArray = std::byte[5];

// Declared span conversion used to reject implicit conversion-based byte borrowing.
// Public methods:
// - operator resource::Input(): Declare span conversion.
struct Conversion {
	operator resource::Input() const noexcept;
};

static_assert(std::is_constructible_v<resource::BytesFile, Array&>);
static_assert(std::is_constructible_v<resource::BytesFile, const Array&>);
static_assert(std::is_constructible_v<resource::BytesFile, CArray&>);
static_assert(std::is_constructible_v<resource::BytesFile, const CArray&>);
static_assert(std::is_constructible_v<resource::BytesFile, resource::Input>);
static_assert(std::is_constructible_v<resource::BytesFile, resource::Output>);
static_assert(std::is_constructible_v<resource::BytesFile, std::span<const std::byte, 5>>);
static_assert(!std::is_constructible_v<resource::BytesFile, Array>);
static_assert(!std::is_constructible_v<resource::BytesFile, const Array>);
static_assert(!std::is_constructible_v<resource::BytesFile, CArray&&>);
static_assert(!std::is_constructible_v<resource::BytesFile, const CArray&&>);
static_assert(!std::is_constructible_v<resource::BytesFile, Conversion&>);
static_assert(!std::is_constructible_v<resource::BytesFile, Conversion>);
static_assert(!std::is_constructible_v<resource::BytesFile, volatile Array&>);
static_assert(!std::is_constructible_v<resource::BytesFile, volatile CArray&>);
static_assert(!std::is_constructible_v<resource::BytesFile, volatile resource::Input&>);
static_assert(!std::is_constructible_v<resource::BytesFile, std::array<unsigned char, 5>&>);

unsigned checks = 0;
#define CHECK(...)                                                                                 \
	do {                                                                                           \
		++checks;                                                                                  \
		if (!(__VA_ARGS__))                                                                        \
			std::abort();                                                                          \
	} while (false)

void checkReadResult(resource::ReadResult result, resource::Status status, resource::Cursor next,
                     std::uint32_t written, bool eof)
{
	CHECK(result.status == status && result.next == next && result.written == written &&
	      result.eof == eof);
}
} // namespace

int main()
{
	// Every offset, an invalid offset and zero through oversized capacities;
	// compare all bytes so errors and output boundaries remain observable.
	for (resource::Cursor cursor = 0; cursor <= source.size() + 1; ++cursor) {
		for (std::size_t capacity = 0; capacity <= source.size() + 1; ++capacity) {
			std::array<std::byte, 10> output;
			output.fill(std::byte{0x7f});
			auto expected = output;
			const auto result = file.read(cursor, resource::Output{output}.subspan(2, capacity));
			if (cursor > source.size()) {
				checkReadResult(result, resource::Status::InvalidCursor, cursor, 0, false);
			} else if (cursor == source.size()) {
				checkReadResult(result, resource::Status::Ok, cursor, 0, true);
			} else if (capacity == 0) {
				checkReadResult(result, resource::Status::BufferTooSmall, cursor, 0, false);
			} else {
				const auto remaining = source.size() - static_cast<std::size_t>(cursor);
				const auto count = capacity < remaining ? capacity : remaining;
				checkReadResult(result, resource::Status::Ok, cursor + count,
				                static_cast<std::uint32_t>(count), cursor + count == source.size());
				for (std::size_t i = 0; i < count; ++i) {
					expected[2 + i] = source[static_cast<std::size_t>(cursor) + i];
				}
			}
			CHECK(output == expected);
		}
	}

	std::array<std::byte, 8> output{};
	for (const auto cursor : {resource::Cursor{UINT32_MAX}, resource::Cursor{UINT32_MAX} + 1,
	                          std::numeric_limits<resource::Cursor>::max()}) {
		checkReadResult(file.read(cursor, output), resource::Status::InvalidCursor, cursor, 0,
		                false);
		CHECK(output == std::array<std::byte, 8>{});
	}
	checkReadResult(empty.read(0, {}), resource::Status::Ok, 0, 0, true);
	checkReadResult(empty.read(1, output), resource::Status::InvalidCursor, 1, 0, false);
	CHECK(output == std::array<std::byte, 8>{});

	// Copying the provider shares its backing storage. It does not snapshot it.
	std::array bytes{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}, std::byte{5}};
	const resource::BytesFile mutableFile{bytes};
	const auto copy = mutableFile;
	bytes[1] = std::byte{42};
	checkReadResult(copy.read(1, resource::Output{output}.first(1)), resource::Status::Ok, 2, 1,
	                false);
	CHECK(output[0] == std::byte{42});

	// memmove must retain correct bytes for output on either side of the source.
	bytes = {std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}, std::byte{5}};
	checkReadResult(mutableFile.read(0, resource::Output{bytes}.subspan(1)), resource::Status::Ok,
	                4, 4, false);
	CHECK(bytes ==
	      (std::array{std::byte{1}, std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}}));
	bytes = {std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}, std::byte{5}};
	checkReadResult(mutableFile.read(1, bytes), resource::Status::Ok, 5, 4, true);
	CHECK(bytes ==
	      (std::array{std::byte{2}, std::byte{3}, std::byte{4}, std::byte{5}, std::byte{5}}));
	checkReadResult(mutableFile.read(0, bytes), resource::Status::Ok, 5, 5, true);

	std::byte cBytes[]{std::byte{66}, std::byte{77}};
	const resource::BytesFile cFile{cBytes};
	const resource::BytesFile fixedSpan{std::span{cBytes}};
	const resource::BytesFile inputSpan{resource::Input{cBytes}};
	CHECK(cFile.size() == 2 && fixedSpan.size() == 2 && inputSpan.size() == 2);
	checkReadResult(cFile.read(0, output), resource::Status::Ok, 2, 2, true);
	CHECK(output[0] == std::byte{66} && output[1] == std::byte{77});

	// A provider added to the ordinary flat table is readable and has no write.
	const auto stat = files.stat(0);
	CHECK(stat.status == resource::Status::Ok && stat.size == source.size() &&
	      stat.flags == resource::FileFlag::Readable);
	checkReadResult(files.read(0, 0, output), resource::Status::Ok, 5, 5, true);
	CHECK(std::equal(source.begin(), source.end(), output.begin()));
	CHECK(files.write(0, 7, {}).status == resource::Status::NotWritable);
	checkReadResult(files.read(2, 9, output), resource::Status::InvalidFile, 9, 0, false);

	std::printf("BytesFile: %u checks passed\n", checks);
}
