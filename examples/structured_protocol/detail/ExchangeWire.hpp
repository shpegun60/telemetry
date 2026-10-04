/*
 * @file ExchangeWire.hpp
 * @brief Explicit little-endian envelope fields and stable wire status mapping.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#ifndef EXAMPLE_STRUCTURED_PROTOCOL_DETAIL_EXCHANGE_WIRE_HPP
#define EXAMPLE_STRUCTURED_PROTOCOL_DETAIL_EXCHANGE_WIRE_HPP
#pragma once

#include "../Binding.hpp"
#include <telemetry/result/EndpointResults.hpp>
#include <concepts>
#include <type_traits>

namespace example::structured_protocol::detail {

// Only used after validating the complete fixed header. Byte operations work
// with unaligned transport storage and never reinterpret a packet as a struct.
template<std::unsigned_integral U>
[[nodiscard]] U load(Input bytes, std::size_t offset) noexcept
{
	using Word = std::conditional_t<(sizeof(U) < sizeof(std::uint32_t)), std::uint32_t, U>;
	Word value = 0;
	for (std::size_t i = 0; i < sizeof(U); ++i)
		value |= Word(std::to_integer<unsigned char>(bytes[offset + i])) << (8 * i);
	return static_cast<U>(value);
}

template<std::unsigned_integral U>
void store(Output bytes, std::size_t offset, U value) noexcept
{
	for (std::size_t i = 0; i < sizeof(U); ++i)
		bytes[offset + i] = static_cast<std::byte>(value >> (8 * i));
}

[[nodiscard]] inline bool magic(Input bytes, const char (&text)[5]) noexcept
{
	for (unsigned i = 0; i < 4; ++i)
		if (bytes[i] != static_cast<std::byte>(text[i]))
			return false;
	return true;
}

inline void magic(Output bytes, const char (&text)[5]) noexcept
{
	for (unsigned i = 0; i < 4; ++i)
		bytes[i] = static_cast<std::byte>(text[i]);
}

// Do not expose enum object bytes. Unknown application statuses must become
// InternalError; named statuses keep these codes if C++ enum layout changes.
inline bool wireStatus(telemetry::WriteResult status, std::uint8_t& wire) noexcept
{
	switch (status) {
		case telemetry::WriteResult::Applied:
			wire = 0;
			return true;
		case telemetry::WriteResult::NotFound:
			wire = 1;
			return true;
		case telemetry::WriteResult::ReadOnly:
			wire = 2;
			return true;
		case telemetry::WriteResult::InvalidValue:
			wire = 3;
			return true;
		case telemetry::WriteResult::Busy:
			wire = 4;
			return true;
		case telemetry::WriteResult::Unavailable:
			wire = 5;
			return true;
	}
	return false;
}

inline bool wireStatus(telemetry::CommandResult status, std::uint8_t& wire) noexcept
{
	switch (status) {
		case telemetry::CommandResult::Executed:
			wire = 0;
			return true;
		case telemetry::CommandResult::Accepted:
			wire = 1;
			return true;
		case telemetry::CommandResult::NotFound:
			wire = 2;
			return true;
		case telemetry::CommandResult::Unavailable:
			wire = 3;
			return true;
		case telemetry::CommandResult::ArgumentCountMismatch:
			wire = 4;
			return true;
		case telemetry::CommandResult::InvalidValue:
			wire = 5;
			return true;
		case telemetry::CommandResult::Busy:
			wire = 6;
			return true;
		case telemetry::CommandResult::Failed:
			wire = 7;
			return true;
	}
	return false;
}

inline bool wireStatus(telemetry::ServiceStatus status, std::uint8_t& wire) noexcept
{
	using S = telemetry::ServiceStatus;
	switch (status) {
		case S::Ok:
			wire = 0;
			return true;
		case S::InvalidArgument:
			wire = 1;
			return true;
		case S::Unavailable:
			wire = 2;
			return true;
		case S::Busy:
			wire = 3;
			return true;
		case S::Failed:
			wire = 4;
			return true;
	}
	return false;
}

inline PacketStatus packetStatus(telemetry::DispatchStatus status) noexcept
{
	using S = telemetry::DispatchStatus;
	switch (status) {
		case S::Ok:
			return PacketStatus::Ok;
		case S::NotFound:
			return PacketStatus::NotFound;
		case S::InvalidPayload:
			return PacketStatus::InvalidPayload;
		case S::BufferTooSmall:
			return PacketStatus::BufferTooSmall;
		case S::WorkspaceTooSmall:
			return PacketStatus::WorkspaceTooSmall;
		case S::InternalError:
			return PacketStatus::InternalError;
		case S::Unavailable:
			return PacketStatus::Unavailable;
	}
	return PacketStatus::InternalError;
}

inline std::uint8_t wireStatus(PacketStatus status) noexcept
{
	using S = PacketStatus;
	switch (status) {
		case S::Ok:
			return 0;
		case S::InvalidRequest:
			return 1;
		case S::UnsupportedVersion:
			return 2;
		case S::NotReady:
			return 3;
		case S::NotFound:
			return 4;
		case S::InvalidPayload:
			return 5;
		case S::BufferTooSmall:
			return 6;
		case S::WorkspaceTooSmall:
			return 7;
		case S::InternalError:
			return 8;
		case S::Unavailable:
			return 9;
	}
	return 8;
}

} // namespace example::structured_protocol::detail
#endif
