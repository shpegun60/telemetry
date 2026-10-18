/*
 * @file Probe.cpp
 * @brief Same DTO encode/decode cost roots for native and generated codecs.
 * @author Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT
 */
#include <device_api/Codec.hpp>
#ifdef CODEGEN_NATIVE
#include <device_api/TelemetryBindings.hpp>
#include <telemetry/codec/Codec.hpp>
#endif
#ifdef CODEGEN_BENCHMARK
#include <chrono>
#include <cstdio>
#endif

using Value = device_api::edge::types::Scalars;

extern "C" [[gnu::noinline]] unsigned codegen_encode(const Value& value,
                                                     std::span<std::byte> bytes) noexcept
{
#ifdef CODEGEN_NATIVE
	return static_cast<unsigned>(telemetry::encode(value, bytes));
#else
	return static_cast<unsigned>(device_api::codec::encode(value, bytes));
#endif
}

extern "C" [[gnu::noinline]] unsigned codegen_decode(std::span<const std::byte> bytes,
                                                     Value& value) noexcept
{
#ifdef CODEGEN_NATIVE
	return static_cast<unsigned>(telemetry::decode(bytes, value));
#else
	return static_cast<unsigned>(device_api::codec::decode(bytes, value));
#endif
}

#ifdef CODEGEN_BENCHMARK
int main()
{
	constexpr unsigned iterations = 200000;
	Value input{}, output{};
	std::array<std::byte, device_api::codec::wireSize<Value>> bytes{};
	std::uint64_t checksum = 0;
	const auto start = std::chrono::steady_clock::now();
	for (unsigned i = 0; i != iterations; ++i) {
		input.u32 = i;
		if (codegen_encode(input, bytes) != 0 || codegen_decode(bytes, output) != 0)
			return 1;
		checksum += output.u32;
	}
	const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
	                         std::chrono::steady_clock::now() - start)
	                         .count();
	if (checksum != std::uint64_t(iterations) * (iterations - 1) / 2)
		return 2;
	std::printf("{\"iterations\":%u,\"encode_decode_ns\":%lld,\"checksum\":%llu}\n", iterations,
	            static_cast<long long>(elapsed), static_cast<unsigned long long>(checksum));
}
#endif
