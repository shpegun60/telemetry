/*
 * @file ArmArrayExpansion.cpp
 * @brief Comparison fixture: 4 KiB array decoded by template expansion.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */

#include <telemetry/codec/Codec.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <new>
#include <span>
#include <utility>

using Big = std::array<std::uint32_t, 1024>;

template<std::size_t... I>
Big expanded(telemetry::codec_detail::Reader& reader, std::index_sequence<I...>) noexcept
{
	return Big{
	    {(static_cast<void>(I), telemetry::codec_detail::decodeValue<std::uint32_t>(reader))...}};
}

extern "C" [[gnu::noinline]] Big* decode_big_expanded(void* storage,
                                                      std::span<const std::byte> input) noexcept
{
	telemetry::codec_detail::Reader reader{input};
	return ::new (storage) Big(expanded(reader, std::make_index_sequence<1024>{}));
}
