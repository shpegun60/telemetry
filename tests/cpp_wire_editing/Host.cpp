/* Exact canonical-byte oracles for the optional, allocation-free writer.
 * Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
 */
#include <wire_edit/Codec.hpp>
#include <cstdio>
#include <cstring>

namespace api = wire_edit;
using Value = api::control::types::Config;
using Empty = api::control::types::Empty;
using Status = api::codec::Status;
static unsigned checks = 0, failures = 0;

static void check(bool condition, unsigned line) noexcept
{
	++checks;
	if (!condition) {
		++failures;
		std::fprintf(stderr, "Host.cpp:%u\n", line);
	}
}

#define CHECK(...) check(bool(__VA_ARGS__), __LINE__)
using Wire = std::array<std::byte, 50>;
static_assert(api::codec::wireSize<Value> == 50);

template<class U, std::size_t N>
static void put(std::array<std::byte, N>& bytes, std::size_t offset, U value) noexcept
{
	for (std::size_t i = 0; i < sizeof(U); ++i)
		bytes[offset + i] = std::byte(static_cast<unsigned char>(value >> (i * 8)));
}

static Wire seed() noexcept
{
	Wire bytes{};
	put(bytes, 0, std::uint32_t{0x12345678});
	for (std::size_t i = 0; i < 3; ++i) {
		put(bytes, 4 + i * 15, std::uint32_t{0x3fa00000});
		put(bytes, 8 + i * 15, std::uint64_t{0x4004000000000000});
		put(bytes, 16 + i * 15, std::uint16_t{0xff85}); // Unknown open enum -123.
		bytes[18 + i * 15] = std::byte{1};
	}
	bytes.back() = std::byte{1};
	return bytes;
}

template<class T, class U, std::size_t N>
static void scalarVectors(const std::array<U, N>& vectors) noexcept
{
	for (const U bits : vectors) {
		std::array<std::byte, sizeof(U)> original{}, output{};
		put(original, 0, bits);
		T value{};
		CHECK(api::codec::decode(original, value) == Status::Ok);
		CHECK(api::codec::encodePreserving(value, original, output) == Status::Ok);
		CHECK(output == original);
		constexpr U fraction = (U{1} << (sizeof(U) == 4 ? 23 : 52)) - U{1};
		constexpr U exponent = (std::numeric_limits<U>::max() >> 1) ^ fraction;
		if ((bits & exponent) == exponent && (bits & fraction) != 0) {
			value = std::numeric_limits<T>::quiet_NaN();
			CHECK(api::codec::encodePreserving(value, original, output) == Status::Ok);
			CHECK(output == original); // Simulates a QVariant conversion that quieted sNaN.
			value = T{2.5};
			std::array<std::byte, sizeof(U)> expected{};
			put(expected, 0, std::bit_cast<U>(value));
			CHECK(api::codec::encodePreserving(value, original, output) == Status::Ok);
			CHECK(output == expected);
		} else {
			value = std::numeric_limits<T>::quiet_NaN();
			std::array<std::byte, sizeof(U)> expected{};
			put(expected, 0, std::bit_cast<U>(value));
			CHECK(api::codec::encodePreserving(value, original, output) == Status::Ok);
			CHECK(output == expected);
		}
	}
	for (const T old : {T{0}, -T{0}}) {
		std::array<std::byte, sizeof(U)> original{}, output{};
		put(original, 0, std::bit_cast<U>(old));
		for (const T value : {T{0}, -T{0}}) {
			std::array<std::byte, sizeof(U)> expected{};
			put(expected, 0, std::bit_cast<U>(value));
			CHECK(api::codec::encodePreserving(value, original, output) == Status::Ok);
			CHECK(output == expected); // Signed zero is the NEW value, never inherited.
		}
	}
}

int main()
{
	const std::array<std::uint32_t, 8> singles{0x7fc12345u, 0xffc54321u, 0x7f812345u, 0xff854321u,
	                                           0,           0x80000000u, 0x7f800000u, 0xff800000u};
	const std::array<std::uint64_t, 8> wides{0x7ff8123456789abcULL,
	                                         0xfff8abcdef123456ULL,
	                                         0x7ff0123456789abcULL,
	                                         0xfff0abcdef123456ULL,
	                                         0,
	                                         0x8000000000000000ULL,
	                                         0x7ff0000000000000ULL,
	                                         0xfff0000000000000ULL};
	scalarVectors<float>(singles);
	scalarVectors<double>(wides);
	for (std::size_t vector = 0; vector < singles.size(); ++vector) {
		Wire original = seed();
		for (std::size_t leaf = 0; leaf < 3; ++leaf) {
			put(original, 4 + leaf * 15, singles[(vector + leaf) % singles.size()]);
			put(original, 8 + leaf * 15, wides[(vector + leaf) % wides.size()]);
		}
		Value value{};
		CHECK(api::codec::decode(original, value) == Status::Ok);
		CHECK(static_cast<std::int16_t>(value.leaves[2].mode) == -123);
		++value.counter;
		Wire expected = original;
		put(expected, 0, std::uint32_t{0x12345679});
		Wire output{};
		CHECK(api::codec::encodePreserving(value, original, output) == Status::Ok);
		CHECK(output == expected);
		CHECK(api::codec::decode(output, value) == Status::Ok);
		std::array<std::byte, 56> paddedSeed{}, paddedOutput{};
		paddedSeed.fill(std::byte{0x5a});
		paddedOutput.fill(std::byte{0xa5});
		std::memcpy(paddedSeed.data() + 1, original.data(), original.size());
		CHECK(api::codec::encodePreserving(value, std::span{paddedSeed}.subspan(1, 50),
		                                   std::span{paddedOutput}.subspan(3, 50)) == Status::Ok);
		CHECK(std::memcmp(paddedOutput.data() + 3, expected.data(), expected.size()) == 0);
		CHECK(paddedOutput.front() == std::byte{0xa5} && paddedOutput.back() == std::byte{0xa5});
		CHECK(paddedSeed.front() == std::byte{0x5a} && paddedSeed.back() == std::byte{0x5a});
		const auto retained = original;
		output[4] ^= std::byte{1};
		CHECK(original == retained);
	}
	auto original = seed();
	Value value{};
	CHECK(api::codec::decode(original, value) == Status::Ok);
	std::array<std::byte, 51> output{};
	output.fill(std::byte{0xa5});
	const auto unchanged = output;
	for (const std::size_t offset :
	     {std::size_t{18}, std::size_t{33}, std::size_t{48}, std::size_t{49}}) {
		auto invalid = original;
		invalid[offset] = std::byte{2};
		CHECK(api::codec::encodePreserving(value, invalid, std::span{output}.first<50>()) ==
		      Status::InvalidValue);
		CHECK(output == unchanged);
	}
	CHECK(api::codec::encodePreserving(value, std::span{original}.first<49>(),
	                                   std::span{output}.first<50>()) == Status::LengthMismatch);
	CHECK(output == unchanged);
	CHECK(api::codec::encodePreserving(value, output, std::span{output}.first<50>()) ==
	      Status::LengthMismatch);
	CHECK(output == unchanged);
	CHECK(api::codec::encodePreserving(value, original, std::span{output}.first<49>()) ==
	      Status::LengthMismatch);
	CHECK(output == unchanged);
	CHECK(api::codec::encodePreserving(value, original, output) == Status::LengthMismatch);
	CHECK(output == unchanged);
	const auto oldOriginal = original;
	CHECK(api::codec::encodePreserving(value, original, original) == Status::Overlap);
	CHECK(original == oldOriginal);
	std::array<std::byte, 51> shared{};
	std::memcpy(shared.data(), original.data(), original.size());
	const auto oldShared = shared;
	CHECK(api::codec::encodePreserving(value, std::span{shared}.first<50>(),
	                                   std::span{shared}.subspan(1, 50)) == Status::Overlap);
	CHECK(shared == oldShared);
	const auto oldNative = std::bit_cast<std::array<std::byte, sizeof(Value)>>(value);
	auto nativeBytes = std::as_writable_bytes(std::span{&value, 1});
	CHECK(api::codec::encodePreserving(value, original, nativeBytes.first<50>()) ==
	      Status::Overlap);
	CHECK(std::memcmp(&value, oldNative.data(), sizeof(Value)) == 0);
	Empty empty{};
	CHECK(api::codec::encodePreserving(empty, {}, {}) == Status::Ok);
	std::array<std::byte, 0> noBytes{};
	std::array<Empty, 2> emptyArray{};
	CHECK(api::codec::encodePreserving(emptyArray, noBytes, noBytes) == Status::Ok);
	std::printf("C++ preserving writer: checks=%u failures=%u\n", checks, failures);
	return failures ? 1 : 0;
}
