/*
 * @file RpcWire.hpp
 * @brief RPC mailbox v1 constants and opaque transaction cursors.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 *
 * This wire contract is separate from descriptor v3 and generic Resource v1.
 * Endpoints remain packed u32 IDs; only the Resource cursor combines a
 * transaction ID with its request or response byte position.
 */
#ifndef RESOURCE_TELEMETRY_V3_RPC_WIRE_HPP
#define RESOURCE_TELEMETRY_V3_RPC_WIRE_HPP
#pragma once

#include <resource/Types.hpp>

namespace resource::telemetry::v3 {

inline constexpr std::uint16_t rpcMajor = 1;
inline constexpr std::uint16_t rpcMinor = 0;
inline constexpr std::uint32_t rpcRequestHeaderBytes = 24;
inline constexpr std::uint32_t rpcReplyHeaderBytes = 32;
inline constexpr std::uint32_t rpcReceiveTimeoutMs = 10000;

enum class RpcOperation : std::uint8_t {
	FieldWrite = 1,
	Command = 2,
	Service = 3,
	FieldRead = 4
};

enum class RpcState : std::uint8_t {
	Idle = 0,
	Receiving = 1,
	Pending = 2,
	Running = 3,
	Ready = 4
};

// Zero starts a transfer. Request and response offsets belong to separate
// streams, so a WRITE next cursor is never used to start the result READ.
[[nodiscard]] constexpr Cursor rpcCursor(std::uint32_t transactionId, std::uint32_t offset) noexcept
{
	return (Cursor{transactionId} << 32) | offset;
}

[[nodiscard]] constexpr std::uint32_t rpcTransactionId(Cursor cursor) noexcept
{
	return static_cast<std::uint32_t>(cursor >> 32);
}

[[nodiscard]] constexpr std::uint32_t rpcOffset(Cursor cursor) noexcept
{
	return static_cast<std::uint32_t>(cursor);
}

} // namespace resource::telemetry::v3
#endif // RESOURCE_TELEMETRY_V3_RPC_WIRE_HPP
