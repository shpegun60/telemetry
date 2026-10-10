/*
 * @file Codec.cpp
 * @brief Encode nested DTOs and use all three standalone decode forms.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */

#include <telemetry/codec/Codec.hpp>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstdint>
#include <span>
#include <type_traits>

namespace ts = telemetry;

namespace {
unsigned checks = 0;
#define CHECK(expression)                                                                          \
	do {                                                                                           \
		++checks;                                                                                  \
		assert((expression));                                                                      \
	} while (false)

// A nested wire DTO. Native padding is absent from the canonical payload.
struct Channel {
	std::uint16_t adc;
	float rms;
};

// Arrays and aggregate members are visited in declaration/element order.
struct Measurement {
	std::array<Channel, 2> channels;
	std::uint32_t timestamp;
	bool valid;
};

void checkMeasurement(const Measurement& actual, const Measurement& expected)
{
	CHECK(actual.channels[0].adc == expected.channels[0].adc);
	CHECK(actual.channels[0].rms == expected.channels[0].rms);
	CHECK(actual.channels[1].adc == expected.channels[1].adc);
	CHECK(actual.channels[1].rms == expected.channels[1].rms);
	CHECK(actual.timestamp == expected.timestamp);
	CHECK(actual.valid == expected.valid);
}
} // namespace

int main()
{
	static_assert(ts::wireSize<Measurement> == 17);
	static_assert(!std::is_copy_constructible_v<ts::Decoded<Measurement>>);
	static_assert(!std::is_move_constructible_v<ts::Decoded<Measurement>>);

	const Measurement source{{{{0x1234, 1.0f}, {0x5678, 2.0f}}}, 0x12345678, true};
	std::array<std::byte, ts::wireSize<Measurement>> bytes{};
	CHECK(ts::encode(source, bytes) == ts::CodecStatus::Ok);
	constexpr std::array<std::byte, 17> expected{
	    std::byte{0x34}, std::byte{0x12}, std::byte{0x00}, std::byte{0x00}, std::byte{0x80},
	    std::byte{0x3f}, std::byte{0x78}, std::byte{0x56}, std::byte{0x00}, std::byte{0x00},
	    std::byte{0x00}, std::byte{0x40}, std::byte{0x78}, std::byte{0x56}, std::byte{0x34},
	    std::byte{0x12}, std::byte{0x01},
	};
	CHECK(bytes == expected);

	// 1. Populate an existing object. No Workspace or second native DTO is used.
	Measurement existing{};
	CHECK(ts::decode(bytes, existing) == ts::CodecStatus::Ok);
	checkMeasurement(existing, source);

	// Validation finishes before the first write, even when a bad bool is last.
	auto malformed = bytes;
	malformed.back() = std::byte{2};
	const Measurement unchanged = existing;
	CHECK(ts::decode(malformed, existing) == ts::CodecStatus::InvalidValue);
	checkMeasurement(existing, unchanged);
	CHECK(ts::decode(std::span<const std::byte>{bytes}.first(bytes.size() - 1), existing) ==
	      ts::CodecStatus::LengthMismatch);
	checkMeasurement(existing, unchanged);

	// 2. Construct directly in caller storage; the result keeps its Lease alive.
	std::array<std::byte, ts::scratchBytes<Measurement>> memory{};
	ts::Workspace workspace{memory};
	{
		auto result = ts::decode<Measurement>(bytes, workspace);
		CHECK(result && result.hasValue() && result.status() == ts::CodecStatus::Ok);
		CHECK(workspace.used() >= sizeof(Measurement));
		checkMeasurement(result.value(), source);
		const auto& immutableResult = result;
		checkMeasurement(immutableResult.value(), source);
		// A reference from result.value() must not outlive this result.
	}
	CHECK(workspace.used() == 0);

	// A failed decode still pins a successful reservation until result destruction.
	{
		auto rejected = ts::decode<Measurement>(malformed, workspace);
		CHECK(!rejected && !rejected.hasValue());
		CHECK(rejected.status() == ts::CodecStatus::InvalidValue);
		CHECK(workspace.used() >= sizeof(Measurement));
	}
	CHECK(workspace.used() == 0);

	// 3. The existing low-level API gives the caller explicit Lease control.
	{
		auto lease = workspace.reserve<Measurement>();
		CHECK(lease.valid() && !lease.constructed());
		Measurement* decoded = nullptr;
		CHECK(ts::decode(bytes, lease, decoded) == ts::CodecStatus::Ok);
		CHECK(decoded == lease.get());
		checkMeasurement(*decoded, source);
		Measurement* second = decoded;
		CHECK(ts::decode(bytes, lease, second) == ts::CodecStatus::InvalidState);
		CHECK(second == nullptr && lease.constructed());
	}
	CHECK(workspace.used() == 0);

	// Results and explicit leases share the same cursor and must release in LIFO order.
	std::array<std::byte, 2 * ts::scratchBytes<Measurement>> nestedMemory{};
	ts::Workspace nested{nestedMemory};
	{
		auto outer = ts::decode<Measurement>(bytes, nested);
		CHECK(outer);
		const auto outerUsage = nested.used();
		{
			auto inner = ts::decode<Measurement>(bytes, nested);
			CHECK(inner && nested.used() > outerUsage);
			checkMeasurement(inner.value(), source);
		}
		CHECK(nested.used() == outerUsage);
		checkMeasurement(outer.value(), source);
	}
	CHECK(nested.used() == 0);

	std::array<std::byte, 1> smallMemory{};
	ts::Workspace small{smallMemory};
	{
		auto result = ts::decode<Measurement>(bytes, small);
		CHECK(!result && result.status() == ts::CodecStatus::WorkspaceTooSmall);
		CHECK(small.used() == 0);
	}

	std::printf("Codec guide: %u checks passed\n", checks);
}
