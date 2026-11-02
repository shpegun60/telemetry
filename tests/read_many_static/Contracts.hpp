/* Independent handwritten scalar Codec and endpoints, with a declared schema. MIT. */
#ifndef TELEMETRY_TESTS_READ_MANY_STATIC_CONTRACTS_HPP
#define TELEMETRY_TESTS_READ_MANY_STATIC_CONTRACTS_HPP
#pragma once
#include <static_many_api/ClientBindings.hpp>
#include <array>
#include <bit>
#include <cstdint>
#include <span>
#include <type_traits>

namespace static_many_contracts {
namespace api = static_many_api;

template<class T, bool = std::is_enum_v<T>>
struct Representation {
	using Type = T;
};

template<class T>
struct Representation<T, true> {
	using Type = std::underlying_type_t<T>;
};

struct Codec {
	static constexpr std::uint64_t fingerprint = api::schema::fingerprint;
	static constexpr std::size_t scratchBytes = 8, scratchAlign = 8;
	template<class T>
	static constexpr std::size_t wireSize = [] {
		if constexpr (std::is_void_v<T>)
			return std::size_t{0};
		else
			return sizeof(T);
	}();

	static std::span<const std::byte> descriptorHeader() noexcept
	{
		return api::schema::descriptorHeader;
	}

	template<class T>
	static std::uint8_t decode(std::span<const std::byte> input, T& value) noexcept
	{
		if (input.size() != sizeof(T))
			return 1;
		std::uint64_t bits = 0;
		for (std::size_t i = 0; i < input.size(); ++i)
			bits |= std::uint64_t{std::to_integer<std::uint8_t>(input[i])} << (i * 8);
		if constexpr (std::is_same_v<T, bool>) {
			if (bits > 1)
				return 2;
			value = bits != 0;
		} else if constexpr (std::is_same_v<T, float>) {
			value = std::bit_cast<float>(static_cast<std::uint32_t>(bits));
		} else if constexpr (std::is_same_v<T, double>) {
			value = std::bit_cast<double>(bits);
		} else {
			using R = typename Representation<T>::Type;
			using U = std::make_unsigned_t<R>;
			value = static_cast<T>(std::bit_cast<R>(static_cast<U>(bits)));
		}
		return 0;
	}

	template<class T>
	static std::uint8_t encode(const T& value, std::span<std::byte> output) noexcept
	{
		if (output.size() != sizeof(T))
			return 1;
		std::uint64_t bits;
		if constexpr (std::is_same_v<T, bool>)
			bits = value ? 1 : 0;
		else if constexpr (std::is_same_v<T, float>)
			bits = std::bit_cast<std::uint32_t>(value);
		else if constexpr (std::is_same_v<T, double>)
			bits = std::bit_cast<std::uint64_t>(value);
		else {
			using R = typename Representation<T>::Type;
			using U = std::make_unsigned_t<R>;
			bits = static_cast<U>(static_cast<R>(value));
		}
		for (std::size_t i = 0; i < output.size(); ++i)
			output[i] = std::byte((bits >> (i * 8)) & 0xffu);
		return 0;
	}
};

template<class T>
struct Field {
	static constexpr unsigned category = 1;
	using Value = T;
	std::uint32_t id = 0, valueTypeId = 0, valueBytes = sizeof(T);
	std::uint8_t capabilities = 1;
	std::uint64_t fingerprint = Codec::fingerprint;

	bool readable() const noexcept
	{
		return (capabilities & 1) != 0;
	}

	bool writable() const noexcept
	{
		return (capabilities & 2) != 0;
	}
};
} // namespace static_many_contracts
#endif
