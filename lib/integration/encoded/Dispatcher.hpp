/*
 * @file Dispatcher.hpp
 * @brief Synchronous complete-frame Encoded v1 bridge to the existing Model API.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 *
 * The owner admits the peer/schema, supplies its trusted nonzero session, and
 * serializes calls and Model/Workspace access. There is no client dependency,
 * queue, retry, duplicate suppression or connection ownership here. Delivering
 * a request twice can invoke an endpoint twice. Request, response and Workspace
 * storage must be disjoint; all borrowed objects outlive this synchronous call.
 */
#ifndef TELEMETRY_INTEGRATION_ENCODED_DISPATCHER_HPP
#define TELEMETRY_INTEGRATION_ENCODED_DISPATCHER_HPP
#pragma once

#include <cstddef>
#include <span>
#include <telemetry/model/Adapter.hpp>
#include <wire/EncodedWire.hpp>

namespace telemetry::integration::encoded {

enum class EncodedDispatchStatus : std::uint8_t {
	Ok,
	Malformed,
	NotAdmitted,
	BufferTooSmall,
	Overlap
};

struct EncodedDispatchResult {
	EncodedDispatchStatus status = EncodedDispatchStatus::Malformed;
	std::size_t written = 0;
	::telemetry::protocol::encoded::Status wire = ::telemetry::protocol::encoded::Status::Ok;

	[[nodiscard]] constexpr explicit operator bool() const noexcept
	{
		return status == EncodedDispatchStatus::Ok;
	}
};

// A valid request produces a correlated reply, including Model refusals.
// Envelope/admission/capacity/overlap failure performs no callback and writes
// no reply. A reply reporting a dispatch error has no payload.
[[nodiscard]] EncodedDispatchResult
dispatchEncoded(::telemetry::ModelView model, ::telemetry::Workspace& workspace,
                std::uint32_t admittedSession, std::span<const std::byte> request,
                std::span<std::byte> response,
                ::telemetry::detail::CurrentStructuredAbiTag) noexcept;

[[nodiscard]] inline EncodedDispatchResult dispatchEncoded(::telemetry::ModelView model,
                                                           ::telemetry::Workspace& workspace,
                                                           std::uint32_t admittedSession,
                                                           std::span<const std::byte> request,
                                                           std::span<std::byte> response) noexcept
{
	return dispatchEncoded(model, workspace, admittedSession, request, response,
	                       ::telemetry::detail::CurrentStructuredAbiTag{});
}

} // namespace telemetry::integration::encoded
#endif // TELEMETRY_INTEGRATION_ENCODED_DISPATCHER_HPP
