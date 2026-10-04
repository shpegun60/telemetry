/*
 * @file AddressProbe.cpp
 * @brief Native object addresses never invoke a DTO's overloaded operator&.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */

#include <telemetry/Telemetry.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <span>
#include <type_traits>
#include <utility>

namespace address_test {

unsigned checks = 0;
unsigned failures = 0;
unsigned payloadAddressCalls = 0;
unsigned resultAddressCalls = 0;
unsigned endpointCalls = 0;

void check(bool condition) noexcept
{
	++checks;
	if (!condition)
		++failures;
}

// Payload overloads operator& to return a decoy; library paths must use its real address.
// API: operator&().
struct Payload {
	std::uint32_t first;
	std::uint32_t second;

	Payload* operator&() noexcept;
	const Payload* operator&() const noexcept;
};

Payload decoy{0xdeadbeefU, 0x12345678U};

Payload* Payload::operator&() noexcept
{
	++payloadAddressCalls;
	return std::addressof(decoy);
}

const Payload* Payload::operator&() const noexcept
{
	++payloadAddressCalls;
	return std::addressof(decoy);
}

// ServiceResult<T> participates in T's associated namespaces. Its own
// self-assignment check must also take an actual address rather than use ADL.
using Result = telemetry::ServiceResult<Payload>;

Result* operator&(Result&) noexcept
{
	++resultAddressCalls;
	return nullptr;
}

const Result* operator&(const Result&) noexcept
{
	++resultAddressCalls;
	return nullptr;
}

static_assert(std::is_aggregate_v<Payload>);
static_assert(std::is_standard_layout_v<Payload>);
static_assert(std::is_trivially_copyable_v<Payload>);
static_assert(std::is_trivially_destructible_v<Payload>);
static_assert(telemetry::Type<Payload>::kind == telemetry::TypeKind::Struct);
static_assert(telemetry::wireSize<Payload> == 8);

struct Nested {
	Payload inner;
	std::uint32_t tail;
};

// Clang's constant-evaluated alignment guard must inspect the physical
// member address without requiring operator& to be constexpr.
static_assert(telemetry::Type<Nested>::kind == telemetry::TypeKind::Struct);
static_assert(telemetry::wireSize<Nested> == 12);

inline constexpr std::array<std::byte, 8> expected{std::byte{4}, std::byte{3}, std::byte{2},
                                                   std::byte{1}, std::byte{8}, std::byte{7},
                                                   std::byte{6}, std::byte{5}};

std::array<std::byte, sizeof(Payload)> decoyBytes() noexcept
{
	std::array<std::byte, sizeof(Payload)> result{};
	std::memcpy(result.data(), static_cast<const void*>(std::addressof(decoy)), result.size());
	return result;
}

bool hasExpectedValue(const Result& result) noexcept
{
	return result.hasValue() && result.value().first == 0x01020304U &&
	       result.value().second == 0x05060708U;
}

Payload reply() noexcept
{
	++endpointCalls;
	return {0x01020304U, 0x05060708U};
}

struct Plain {
	std::uint32_t first;
	std::uint32_t second;
};

Plain echo(const Payload& request) noexcept
{
	++endpointCalls;
	return {request.first, request.second};
}

void codecAddress() noexcept
{
	struct Envelope {
		Payload value;
		Payload tail;
	};

	Envelope envelope{{0x01020304U, 0x05060708U}, {0U, 0U}};
	const auto original = std::as_bytes(std::span{std::addressof(envelope), 1});
	std::array<std::byte, sizeof(Envelope)> originalBytes{};
	std::memcpy(originalBytes.data(), original.data(), original.size());
	const auto before = decoyBytes();
	auto memory = std::as_writable_bytes(std::span{std::addressof(envelope), 1});

	// The output intersects the second member, so a missed check would modify
	// a member before the encoder has read it. Rejection must precede writes.
	const auto overlap = memory.subspan(offsetof(Envelope, value) + 4, 8);
	check(telemetry::encode(envelope.value, overlap) == telemetry::CodecStatus::Overlap);
	check(std::memcmp(originalBytes.data(), original.data(), original.size()) == 0);
	check(decoyBytes() == before);
	check(payloadAddressCalls == 0);

	std::array<std::byte, 8> bytes{};
	check(telemetry::encode(envelope.value, bytes) == telemetry::CodecStatus::Ok);
	check(bytes == expected);
	check(decoyBytes() == before);
	check(payloadAddressCalls == 0);

	Nested nested{{0x01020304U, 0x05060708U}, 0x0c0b0a09U};
	std::array<std::byte, 12> nestedBytes{};
	check(telemetry::encode(nested, nestedBytes) == telemetry::CodecStatus::Ok);
	check(nestedBytes[8] == std::byte{9} && nestedBytes[11] == std::byte{12});
	check(payloadAddressCalls == 0);
}

void resultAddresses()
{
	const auto before = decoyBytes();
	{
		auto source = Result::successFrom([]() noexcept -> Payload {
			return {0x01020304U, 0x05060708U};
		});
		check(hasExpectedValue(source));
		check(source.valueOrNull() == std::addressof(source.value()));
		const auto& constant = source;
		check(constant.valueOrNull() == std::addressof(constant.value()));
		check(decoyBytes() == before && payloadAddressCalls == 0);

		auto copy = source;
		check(hasExpectedValue(copy));
		check(copy.valueOrNull() == std::addressof(copy.value()));
		auto moved = std::move(copy);
		check(hasExpectedValue(moved));
		check(moved.valueOrNull() == std::addressof(moved.value()));
		check(decoyBytes() == before && payloadAddressCalls == 0);

		auto destination = Result::failure(telemetry::ServiceStatus::Busy);
		check(destination.valueOrNull() == nullptr);
		destination = source; // Failure -> success constructs a new payload.
		check(hasExpectedValue(destination));
		destination = moved; // Success -> success uses native assignment.
		check(hasExpectedValue(destination));
		destination = Result::failure(telemetry::ServiceStatus::Failed);
		check(!destination.hasValue() && destination.valueOrNull() == nullptr);
		check(destination.status() == telemetry::ServiceStatus::Failed);
		destination = source;
		check(hasExpectedValue(destination));
		auto failed = Result::failure(telemetry::ServiceStatus::Unavailable);
		destination = failed; // Copy success -> failure destroys the payload.
		check(!destination.hasValue() && destination.valueOrNull() == nullptr);
		destination = std::move(source); // Move failure -> success constructs it.
		check(hasExpectedValue(destination));
		destination = std::move(moved); // Move success -> success assignment.
		check(hasExpectedValue(destination));

		const Result* alias = std::addressof(destination);
		destination = *alias;
		check(hasExpectedValue(destination));
		Result* mutableAlias = std::addressof(destination);
		destination = std::move(*mutableAlias);
		check(hasExpectedValue(destination));
		check(resultAddressCalls == 0);
		check(decoyBytes() == before && payloadAddressCalls == 0);
	}
	// Includes destruction of every still-successful local result.
	check(decoyBytes() == before);
	check(payloadAddressCalls == 0);
	check(resultAddressCalls == 0);
}

void encodedAddresses() noexcept
{
	constexpr telemetry::ServiceTable table{telemetry::service<&reply>("Reply"),
	                                        telemetry::service<&echo>("Echo")};
	std::array<std::byte, 128> scratch{};
	telemetry::Workspace workspace{scratch};
	const auto before = decoyBytes();
	std::array<std::byte, 8> bytes{};
	const auto owning = table.data()[0].callEncoded({}, bytes, workspace);
	check(owning.dispatch == telemetry::DispatchStatus::Ok);
	check(owning.endpointStatus == telemetry::ServiceStatus::Ok);
	check(owning.written == bytes.size());
	check(bytes == expected);
	check(endpointCalls == 1);
	check(decoyBytes() == before && payloadAddressCalls == 0);

	bytes.fill(std::byte{0});
	const auto request = table.data()[1].callEncoded(expected, bytes, workspace);
	check(request.dispatch == telemetry::DispatchStatus::Ok);
	check(request.endpointStatus == telemetry::ServiceStatus::Ok);
	check(request.written == bytes.size());
	check(bytes == expected);
	check(endpointCalls == 2);
	check(decoyBytes() == before && payloadAddressCalls == 0);

	bytes.fill(std::byte{0x55});
	const auto tooSmall =
	    table.data()[1].callEncoded(expected, std::span<std::byte>{bytes}.first(7), workspace);
	check(tooSmall.dispatch == telemetry::DispatchStatus::BufferTooSmall);
	check(tooSmall.written == 0);
	check(endpointCalls == 2);
	check(std::all_of(bytes.begin(), bytes.end(), [](std::byte value) noexcept {
		return value == std::byte{0x55};
	}));
	check(decoyBytes() == before && payloadAddressCalls == 0);
}

} // namespace address_test

int main()
{
	address_test::codecAddress();
	address_test::resultAddresses();
	address_test::encodedAddresses();
	std::printf("{\"checks\":%u,\"failures\":%u}\n", address_test::checks, address_test::failures);
	return address_test::failures == 0 ? 0 : 1;
}
