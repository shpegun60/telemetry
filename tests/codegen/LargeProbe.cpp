/*
 * @file LargeProbe.cpp
 * @brief Caller-owned 10,001-byte generated DTO encode/decode stack roots.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#include <sdk/device/Codec.hpp>

using Large = sdk::device::motor::types::LargeRequest;
static_assert(sdk::device::codec::wireSize<Large> == 10001);

extern "C" [[gnu::noinline]] unsigned codegen_large_encode(const Large& value,
                                                           std::span<std::byte> bytes) noexcept
{
	return static_cast<unsigned>(sdk::device::codec::encode(value, bytes));
}

extern "C" [[gnu::noinline]] unsigned codegen_large_decode(std::span<const std::byte> bytes,
                                                           Large& value) noexcept
{
	return static_cast<unsigned>(sdk::device::codec::decode(bytes, value));
}
