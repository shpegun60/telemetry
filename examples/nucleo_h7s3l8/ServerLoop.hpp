/**
 * @file ServerLoop.hpp
 * @brief One-owner dispatch gate shared by the board and host contract checks.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#ifndef TELEMETRY_EXAMPLES_NUCLEO_H7S3L8_SERVER_LOOP_HPP
#define TELEMETRY_EXAMPLES_NUCLEO_H7S3L8_SERVER_LOOP_HPP
#pragma once
#include "Protocol.hpp"
#include "../inspector_serial/SerialProfile.hpp"
#include <cobs/Cobs.h>

namespace nucleo_demo {
enum class Action {
	Idle,
	Busy,
	Dispatched,
	Sent,
	Refused,
	TxFailed,
	Capacity
};

// Caller drives RX and endpoint.poll first, then invokes step() from exactly
// one loop. No dequeue, Model call or fresh LIST can overtake a physical TX
// borrow, an unreclaimed COBS lease, or a pending reply. Endpoint and serial
// outlive this object. There is no independent execution queue here.
template<class Serial, class Endpoint>
class ServerLoop {
public:
	ServerLoop(Serial& serial, Endpoint& endpoint) noexcept : serial_(serial), endpoint_(endpoint)
	{}

	[[nodiscard]] Action step(std::uint32_t now) noexcept
	{
		if (serial_.tx_busy() || endpoint_.tx_active())
			return Action::Busy;
		if (pending_) {
			const auto result = endpoint_.send(pending_);
			if (result == wire::SendResult::Sent)
				return Action::Sent;
			if (result == wire::SendResult::Busy)
				return Action::Busy;
			pending_ = {};
			return Action::TxFailed; // never replay a failed UART transfer
		}
		auto packet = endpoint_.pop_packet();
		if (!packet)
			return Action::Idle;
		const auto bytes = packet.data();
		const auto written = protocol_.dispatch(
		    {reinterpret_cast<const std::byte*>(bytes.data()), bytes.size()}, reply_, now);
		if (!written)
			return Action::Refused;
		pending_ = endpoint_.make_message(written);
		if (!pending_ ||
		    !pending_.append_bytes({reinterpret_cast<const std::uint8_t*>(reply_.data()), written}))
			return Action::Capacity;
		return Action::Dispatched;
	}

private:
	Serial& serial_;
	Endpoint& endpoint_;
	Protocol protocol_;
	std::array<std::byte, inspector_serial::maxBodyBytes> reply_{};
	typename Endpoint::Message pending_;
};
} // namespace nucleo_demo
#endif // TELEMETRY_EXAMPLES_NUCLEO_H7S3L8_SERVER_LOOP_HPP
