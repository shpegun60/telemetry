/* Independent handwritten DTO Codec with declared generated schema identity. MIT. */
#ifndef TELEMETRY_TESTS_BATCH_STATIC_CONTRACTS_HPP
#define TELEMETRY_TESTS_BATCH_STATIC_CONTRACTS_HPP
#pragma once
#include <static_batch_api/ClientBindings.hpp>
#include <bit>
#include <tuple>
#include <type_traits>

namespace static_batch_contracts {
namespace api = static_batch_api;
using Data = api::control::types::Config;
using Empty = api::control::types::Empty;
using Flag = api::control::types::FlagResponse;

template<class T, bool = std::is_enum_v<T>>
struct Representation {
	using Type = T;
};

template<class T>
struct Representation<T, true> {
	using Type = std::underlying_type_t<T>;
};

template<class T>
struct Array : std::false_type {};

template<class T, std::size_t N>
struct Array<std::array<T, N>> : std::true_type {
	using Element = T;
	static constexpr std::size_t count = N;
};

template<class T>
auto members(T& value) noexcept
{
	return std::tie(value.number_value, value.enabled, value.gains, value.mode, value.large,
	                value.negative, value.f32, value.f64);
}

struct Codec {
	static constexpr std::uint64_t fingerprint = api::schema::fingerprint;
	static constexpr std::size_t scratchBytes = sizeof(Data), scratchAlign = alignof(Data);

	static auto descriptorHeader() noexcept
	{
		return std::span<const std::byte>{api::schema::descriptorHeader};
	}
	template<class T>
	static constexpr std::size_t wireSize = [] {
		if constexpr (std::is_void_v<T> || std::is_same_v<T, Empty>)
			return std::size_t{0};
		else if constexpr (std::is_same_v<T, Data>)
			return std::size_t{41};
		else if constexpr (std::is_same_v<T, Flag>)
			return std::size_t{1};
		else if constexpr (Array<T>::value)
			return Array<T>::count * sizeof(typename Array<T>::Element);
		else
			return sizeof(T);
	}();

	template<class T>
	static std::uint8_t encode(const T& value, std::span<std::byte> output) noexcept
	{
		if (output.size() != wireSize<T>)
			return 1;
		if constexpr (std::is_same_v<T, Empty>)
			return 0;
		else if constexpr (std::is_same_v<T, Flag>)
			return encode(value.value, output);
		else if constexpr (std::is_same_v<T, Data> || Array<T>::value) {
			std::size_t offset = 0;
			std::uint8_t status = 0;
			auto one = [&](const auto& leaf) noexcept {
				using Leaf = std::remove_cvref_t<decltype(leaf)>;
				status |= encode(leaf, output.subspan(offset, wireSize<Leaf>));
				offset += wireSize<Leaf>;
			};
			if constexpr (std::is_same_v<T, Data>)
				std::apply(
				    [&](const auto&... leaf) noexcept {
					    (one(leaf), ...);
				    },
				    members(value));
			else
				for (const auto& leaf : value)
					one(leaf);
			return status;
		} else {
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
				output[i] = std::byte((bits >> (8 * i)) & 255u);
			return 0;
		}
	}

	template<class T>
	static std::uint8_t decode(std::span<const std::byte> input, T& value) noexcept
	{
		if (input.size() != wireSize<T>)
			return 1;
		if constexpr (std::is_same_v<T, Empty>)
			return 0;
		else if constexpr (std::is_same_v<T, Flag>)
			return decode(input, value.value);
		else if constexpr (std::is_same_v<T, Data> || Array<T>::value) {
			std::size_t offset = 0;
			std::uint8_t status = 0;
			T temporary{};
			auto one = [&](auto& leaf) noexcept {
				using Leaf = std::remove_cvref_t<decltype(leaf)>;
				status |= decode(input.subspan(offset, wireSize<Leaf>), leaf);
				offset += wireSize<Leaf>;
			};
			if constexpr (std::is_same_v<T, Data>)
				std::apply(
				    [&](auto&... leaf) noexcept {
					    (one(leaf), ...);
				    },
				    members(temporary));
			else
				for (auto& leaf : temporary)
					one(leaf);
			if (!status)
				value = temporary;
			return status;
		} else {
			std::uint64_t bits = 0;
			for (std::size_t i = 0; i < input.size(); ++i)
				bits |= std::uint64_t{std::to_integer<std::uint8_t>(input[i])} << (8 * i);
			if constexpr (std::is_same_v<T, bool>) {
				if (bits > 1)
					return 2;
				value = bits != 0;
			} else if constexpr (std::is_same_v<T, float>)
				value = std::bit_cast<float>(std::uint32_t(bits));
			else if constexpr (std::is_same_v<T, double>)
				value = std::bit_cast<double>(bits);
			else {
				using R = typename Representation<T>::Type;
				using U = std::make_unsigned_t<R>;
				value = static_cast<T>(std::bit_cast<R>(static_cast<U>(bits)));
			}
			return 0;
		}
	}
};
} // namespace static_batch_contracts
#endif
