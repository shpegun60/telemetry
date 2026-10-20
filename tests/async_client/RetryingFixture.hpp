/* Real Model, canonical typed policy and owner-only retry test transport. MIT. */
#ifndef TELEMETRY_TESTS_ASYNC_CLIENT_RETRYING_FIXTURE_HPP
#define TELEMETRY_TESTS_ASYNC_CLIENT_RETRYING_FIXTURE_HPP
#pragma once
#include "EncodedFixture.hpp"
#include <client/DeviceClient.hpp>
#include <integration/encoded/RetryingBackend.hpp>
#include <telemetry/descriptor/Descriptor.hpp>

namespace retrying_fixture {
namespace cl = ::telemetry::client;
namespace encoded = ::telemetry::integration::encoded;
using Config = encoded_fixture::Config;

struct Policy {
	template<class T>
	static constexpr std::size_t wireSize = ::telemetry::wireSize<T>;
	static constexpr std::size_t scratchBytes = sizeof(Config), scratchAlign = alignof(Config);
	inline static std::array<std::byte, 64> header{};

	static cl::Input descriptorHeader() noexcept
	{
		return header;
	}

	template<class T>
	static std::uint8_t encode(const T& value, cl::Output bytes) noexcept
	{
		return static_cast<std::uint8_t>(::telemetry::encode(value, bytes));
	}

	template<class T>
	static std::uint8_t decode(cl::Input bytes, T& value) noexcept
	{
		return static_cast<std::uint8_t>(::telemetry::decode(bytes, value));
	}
};

struct Field {
	static constexpr unsigned category = 1;
	using Value = Config;
	std::uint32_t id = 0;

	bool readable() const noexcept
	{
		return true;
	}

	bool writable() const noexcept
	{
		return true;
	}
};

struct Command {
	static constexpr unsigned category = 2;
	using Request = Config;
	std::uint32_t id = 0;
};

struct Service {
	static constexpr unsigned category = 3;
	using Request = Config;
	using Response = Config;
	std::uint32_t id = 0;
};

using Client = cl::DeviceClient<Policy, 4, 64>;
} // namespace retrying_fixture
#endif // TELEMETRY_TESTS_ASYNC_CLIENT_RETRYING_FIXTURE_HPP
