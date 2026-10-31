/*
 * @file Framing.hpp
 * @brief Count actual frames with the existing Inspector serial COBS profile.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#ifndef TELEMETRY_EXAMPLES_READ_COMPARISON_FRAMING_HPP
#define TELEMETRY_EXAMPLES_READ_COMPARISON_FRAMING_HPP
#pragma once

#include "../inspector_serial/SerialProfile.hpp"
#include <cobs/Cobs.h>
#include <cstddef>
#include <cstdint>
#include <span>

namespace read_comparison {

// This is an accounting pass, not a transport or timing benchmark. The sender
// consumes only the encoded size and never retains its borrowed wire bytes.
// Zero denotes failure; even an empty body has a nonempty framed representation.
[[nodiscard]] inline std::size_t framedBytes(std::span<const std::byte> body) noexcept
{
	if (body.size() > inspector_serial::maxBodyBytes)
		return 0;
	using Link = cobs::Endpoint<wire::Pool<1, 1>, inspector_serial::FrameFormat>;
	// Bound the helper's local fixed pool independently of payload contents.
	static_assert(sizeof(Link) <= 8192);
	Link encoder;
	std::size_t size = 0;
	if (!encoder.bind(
	        [&size](std::span<const std::uint8_t> frame) noexcept {
		        if (frame.empty() || frame.back() != 0 || size != 0)
			        return false;
		        size = frame.size();
		        return true;
	        },
	        []() noexcept {
		        return false;
	        }))
		return 0;
	auto message = encoder.make_message(body.size());
	if (!message ||
	    !message.append_bytes({reinterpret_cast<const std::uint8_t*>(body.data()), body.size()}))
		return 0;
	if (encoder.send(message) != wire::SendResult::Sent)
		return 0;
	encoder.poll(0);
	return !encoder.tx_active() ? size : 0;
}

} // namespace read_comparison
#endif // TELEMETRY_EXAMPLES_READ_COMPARISON_FRAMING_HPP
