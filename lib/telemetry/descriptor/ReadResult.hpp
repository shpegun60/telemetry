/*
 * @file ReadResult.hpp
 * @brief Resource-independent outcomes for canonical descriptor byte reads.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#ifndef TELEMETRY_DESCRIPTOR_READ_RESULT_HPP
#define TELEMETRY_DESCRIPTOR_READ_RESULT_HPP
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace telemetry::descriptor {

using Cursor = std::uint64_t;
using Output = std::span<std::byte>;

enum class ReadStatus : std::uint8_t {
	Ok,
	InvalidCursor,
	BufferTooSmall,
	InvalidData
};

// Byte offsets and counts have the same widths on every supported ABI.
// Constructor arguments describe the result independently of member layout.
struct ReadResult {
	Cursor next = 0;
	std::uint32_t written = 0;
	ReadStatus status = ReadStatus::Ok;
	bool eof = false;

	constexpr ReadResult(ReadStatus resultStatus = ReadStatus::Ok, Cursor nextCursor = 0,
	                     std::uint32_t byteCount = 0, bool atEnd = false) noexcept
	    : next(nextCursor), written(byteCount), status(resultStatus), eof(atEnd)
	{}
};

} // namespace telemetry::descriptor
#endif
