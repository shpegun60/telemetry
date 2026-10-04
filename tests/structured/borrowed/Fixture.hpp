/* Common DTOs only; no generated data or device operations. MIT. */

#ifndef TELEMETRY_TESTS_STRUCTURED_BORROWED_FIXTURE_HPP
#define TELEMETRY_TESTS_STRUCTURED_BORROWED_FIXTURE_HPP
#pragma once

#include <telemetry/Telemetry.hpp>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <type_traits>

namespace borrowed_fixture {
namespace ts = ::telemetry;
using DS = ts::DispatchStatus;
using SS = ts::ServiceStatus;
using WR = ts::WriteResult;

// Small request DTO: boolean validation plus the deterministic payload seed.
struct Request {
	bool ready;
	std::uint8_t seed;
};

// Canonical 5-byte payload used to compare owning and borrowed result identity.
struct Config {
	std::uint32_t value;
	bool enabled;
	friend bool operator==(const Config&, const Config&) = default;
};
enum class Mode : std::int16_t {
	Off = -1,
	On = 2
};

// Fixed-size payload whose full bytes are compared at 4 KiB and 64 KiB scales.
template<std::size_t N>
struct Blob {
	std::array<std::uint8_t, N> bytes;
	friend bool operator==(const Blob&, const Blob&) = default;
};

template<std::size_t N>
void fill(Blob<N>& value, unsigned seed) noexcept
{
	for (std::size_t i = 0; i < N; ++i)
		value.bytes[i] = static_cast<std::uint8_t>((i * 37u + seed) & 255u);
}

template<std::size_t N>
bool pattern(std::span<const std::byte> wire, unsigned seed) noexcept
{
	if (wire.size() != N)
		return false;
	for (std::size_t i = 0; i < N; ++i)
		if (wire[i] != static_cast<std::byte>((i * 37u + seed) & 255u))
			return false;
	return true;
}

// Counted owner for paired ownership policies.
// API: get() borrows T; own() copies T; set() mutates T and counts writes.
template<class T>
struct Owner {
	T value;
	mutable unsigned reads = 0;
	unsigned writes = 0;

	const T& get() const noexcept
	{
		++reads;
		return value;
	}

	T own() const noexcept
	{
		++reads;
		return value;
	}

	WR set(const T& next) noexcept
	{
		++writes;
		value = next;
		return WR::Applied;
	}
};

// Stable owner used by functions, methods, context callbacks and slots.
// API: get() is a borrowed Field getter; call()/noRequest() are borrowed Services.
struct Device {
	Config value{0x12345678u, true};
	mutable unsigned reads = 0, calls = 0;

	const Config& get() const noexcept
	{
		++reads;
		return value;
	}

	const Config& call(const Request&) const noexcept
	{
		++calls;
		return value;
	}

	const Config& noRequest() const noexcept
	{
		++calls;
		return value;
	}
};

inline Device first{}, second{{0x87654321u, false}};

inline const Config& getFirst() noexcept
{
	return first.get();
}

inline const Config& getSecond() noexcept
{
	return second.get();
}

inline const Config& callFirst(const Request& request) noexcept
{
	return first.call(request);
}

inline const Config& callSecond(const Request& request) noexcept
{
	return second.call(request);
}

inline const Config& contextGet(void* context) noexcept
{
	return static_cast<Device*>(context)->get();
}

inline const Config& contextCall(void* context, const Request& request) noexcept
{
	return static_cast<Device*>(context)->call(request);
}

inline bool configBytes(std::span<const std::byte> bytes, const Config& value) noexcept
{
	if (bytes.size() != 5)
		return false;
	for (unsigned i = 0; i < 4; ++i)
		if (bytes[i] != static_cast<std::byte>((value.value >> (8u * i)) & 255u))
			return false;
	return bytes[4] == (value.enabled ? std::byte{1} : std::byte{0});
}
} // namespace borrowed_fixture

#endif // TELEMETRY_TESTS_STRUCTURED_BORROWED_FIXTURE_HPP
