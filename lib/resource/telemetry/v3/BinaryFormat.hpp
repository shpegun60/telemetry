/*
 * @file BinaryFormat.hpp
 * @brief Canonical structural descriptor v3.0 wire constants.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 *
 * Keep the producer and client on one frozen descriptor and values layout.
 * These numbers describe canonical bytes, independently of C++ object
 * layout; version changes must be coordinated with descriptor readers.
 */
#ifndef RESOURCE_TELEMETRY_V3_BINARY_FORMAT_HPP
#define RESOURCE_TELEMETRY_V3_BINARY_FORMAT_HPP
#pragma once

#include <cstdint>

namespace resource::telemetry::v3 {

inline constexpr std::uint16_t binaryMajor = 3;
inline constexpr std::uint16_t binaryMinor = 0;
inline constexpr std::uint16_t descriptorHeaderBytes = 64;
// Values carry the cached descriptor fingerprint, never a hash of live data.
inline constexpr std::uint16_t valuesHeaderBytes = 24;
enum class ValueStatus : std::uint8_t {
	Ok = 0,
	Unavailable = 1
};
inline constexpr std::uint8_t recordVersion = 1;
inline constexpr std::uint32_t recordHeaderBytes = 8;
inline constexpr std::uint64_t fingerprintBasis = 0xcbf29ce484222325ULL;
inline constexpr std::uint64_t fingerprintPrime = 0x100000001b3ULL;

enum class RecordKind : std::uint8_t {
	Type = 1,
	Catalog,
	Field,
	Command,
	Service
};
enum class Category : std::uint8_t {
	Field = 1,
	Command,
	Service
};
enum class Capability : std::uint8_t {
	Readable = 1,
	Writable = 2
};

} // namespace resource::telemetry::v3
#endif
