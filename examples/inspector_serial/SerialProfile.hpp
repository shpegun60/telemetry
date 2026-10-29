/*
 * @file SerialProfile.hpp
 * @brief Shared host and board COBS geometry for ordinary Resource and TEQ1 bodies.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#ifndef TELEMETRY_EXAMPLES_INSPECTOR_SERIAL_SERIAL_PROFILE_HPP
#define TELEMETRY_EXAMPLES_INSPECTOR_SERIAL_SERIAL_PROFILE_HPP
#pragma once

#include <cobs/Format.h>
#include <crc/Crc.h>
#include <cstddef>
#include <cstdint>

namespace inspector_serial {
inline constexpr std::size_t maxBodyBytes = 2048;
inline constexpr std::uint32_t minBaudRate = 1200, maxBaudRate = 4000000;
using FrameFormat = cobs::Format<crc::Crc16Bitwise, maxBodyBytes>;
static_assert(FrameFormat::length_size == 2 && FrameFormat::crc_size == 2);

// Absolute exchange budget: worst-case COBS framing in both directions,
// 8N1 airtime rounded up, plus a finite processing/scheduling allowance.
// Zero rejects inputs outside this example's bounded serial profile.
[[nodiscard]] constexpr std::uint32_t exchangeTimeoutMs(std::size_t requestBody,
                                                        std::size_t responseBody,
                                                        std::uint32_t baud,
                                                        std::uint32_t marginMs) noexcept
{
	if (requestBody > maxBodyBytes || responseBody > maxBodyBytes || baud < minBaudRate ||
	    baud > maxBaudRate)
		return 0;
	const auto bytes = std::uint64_t{FrameFormat::tx_storage_size_for_capacity(requestBody)} +
	                   FrameFormat::tx_storage_size_for_capacity(responseBody);
	const auto timeout = (bytes * 10000u + baud - 1u) / baud + marginMs;
	return timeout <= 60000u ? static_cast<std::uint32_t>(timeout) : 0u;
}
} // namespace inspector_serial
#endif // TELEMETRY_EXAMPLES_INSPECTOR_SERIAL_SERIAL_PROFILE_HPP
