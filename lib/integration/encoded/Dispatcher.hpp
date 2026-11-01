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
#include <wire/ReadManyWire.hpp>

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

namespace detail {
// Only the new compiled overload consumes this record across a module boundary.
// Preserve the four existing operations' symbol while checking both new inputs
// and the returned integration result for callers with different packing flags.
template<std::size_t... Parts>
struct ReadManyDispatchLayoutTag {};

using ReadManyCapability = ::telemetry::protocol::encoded::read_many::Capability;
using CurrentReadManyDispatchLayoutTag = ReadManyDispatchLayoutTag<
    sizeof(ReadManyCapability), alignof(ReadManyCapability), offsetof(ReadManyCapability, session),
    offsetof(ReadManyCapability, fingerprint), offsetof(ReadManyCapability, itemLimit),
    offsetof(ReadManyCapability, requestLimit), offsetof(ReadManyCapability, replyLimit),
    sizeof(EncodedDispatchResult), alignof(EncodedDispatchResult),
    offsetof(EncodedDispatchResult, status), offsetof(EncodedDispatchResult, written),
    offsetof(EncodedDispatchResult, wire)>;
} // namespace detail

// A valid request produces a correlated reply, including Model refusals.
// Envelope/admission/capacity/overlap failure performs no callback and writes
// no reply. A reply reporting a dispatch error has no payload.
// This compatibility overload does not admit optional ReadMany requests.
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

// Explicitly admit the optional scalar ReadMany mapping. The owner guarantees
// that this capability's fingerprint describes model and remains immutable for
// admittedSession. It is not inferred from receiving operation 5. The capability
// is ignored for the four existing endpoint operations, preserving their rules.
// Request, response, Workspace and capability control storage remain disjoint.
[[nodiscard]] EncodedDispatchResult
dispatchEncoded(::telemetry::ModelView model, ::telemetry::Workspace& workspace,
                std::uint32_t admittedSession, std::span<const std::byte> request,
                std::span<std::byte> response,
                const ::telemetry::protocol::encoded::read_many::Capability& readMany,
                ::telemetry::detail::CurrentStructuredAbiTag,
                detail::CurrentReadManyDispatchLayoutTag) noexcept;

[[nodiscard]] inline EncodedDispatchResult
dispatchEncoded(::telemetry::ModelView model, ::telemetry::Workspace& workspace,
                std::uint32_t admittedSession, std::span<const std::byte> request,
                std::span<std::byte> response,
                const ::telemetry::protocol::encoded::read_many::Capability& readMany) noexcept
{
	return dispatchEncoded(model, workspace, admittedSession, request, response, readMany,
	                       ::telemetry::detail::CurrentStructuredAbiTag{},
	                       detail::CurrentReadManyDispatchLayoutTag{});
}

} // namespace telemetry::integration::encoded
#endif // TELEMETRY_INTEGRATION_ENCODED_DISPATCHER_HPP
