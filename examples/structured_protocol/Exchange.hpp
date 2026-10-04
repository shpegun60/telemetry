/*
 * @file Exchange.hpp
 * @brief Complete packet dispatch over an already agreed structured model.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 *
 * Route an agreed peer to the existing encoded endpoint operations.
 * Capacity and decoding checks precede application callbacks; request IDs
 * correlate replies and do not suppress repeated execution.
 */
#ifndef EXAMPLE_STRUCTURED_PROTOCOL_EXCHANGE_HPP
#define EXAMPLE_STRUCTURED_PROTOCOL_EXCHANGE_HPP
#pragma once

#include "Binding.hpp"

namespace example::structured_protocol {

inline constexpr std::uint32_t exchangeHeaderBytes = 24;
enum class Operation : std::uint8_t {
	FieldWrite = 1,
	Command = 2,
	Service = 3
};

// Public methods:
// - process(): Route agreed packet.
class Exchange {
public:
	// A whole request is required. Response may overlap it, including partial
	// overlap: routing and native request decoding finish before response writes.
	// Calling again executes again; requestId is correlation, not deduplication.
	[[nodiscard]] static PacketResult process(const Binding& peer, Input request, Output response,
	                                          telemetry::Workspace& workspace) noexcept
	{
		return processImpl(peer, request, response, workspace, detail::CurrentExchangeAbiTag{});
	}

private:
	[[nodiscard]] static PacketResult processImpl(const Binding&, Input, Output,
	                                              telemetry::Workspace&,
	                                              detail::CurrentExchangeAbiTag) noexcept;
};

} // namespace example::structured_protocol
#endif
