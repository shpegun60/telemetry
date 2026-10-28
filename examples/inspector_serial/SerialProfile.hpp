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

namespace inspector_serial {
inline constexpr std::size_t maxBodyBytes = 2048;
using FrameFormat = cobs::Format<crc::Crc16Bitwise, maxBodyBytes>;
static_assert(FrameFormat::length_size == 2 && FrameFormat::crc_size == 2);
} // namespace inspector_serial
#endif // TELEMETRY_EXAMPLES_INSPECTOR_SERIAL_SERIAL_PROFILE_HPP
