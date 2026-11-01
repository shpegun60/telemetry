/*
 * @file BatchDispatcher.hpp
 * @brief Explicit optional TBM1 admission for complete Encoded mixed batches.
 * @author Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT
 */
#ifndef TELEMETRY_INTEGRATION_ENCODED_BATCH_DISPATCHER_HPP
#define TELEMETRY_INTEGRATION_ENCODED_BATCH_DISPATCHER_HPP
#pragma once

#include <integration/encoded/Dispatcher.hpp>
#include <wire/BatchWire.hpp>

namespace telemetry::integration::encoded {
namespace detail {
template<std::size_t... Parts>
struct BatchDispatchLayoutTag {};

using BatchCapability = ::telemetry::protocol::encoded::batch::Capability;
using CurrentBatchDispatchLayoutTag = BatchDispatchLayoutTag<
    sizeof(BatchCapability), alignof(BatchCapability), offsetof(BatchCapability, session),
    offsetof(BatchCapability, fingerprint), offsetof(BatchCapability, itemLimit),
    offsetof(BatchCapability, requestLimit), offsetof(BatchCapability, replyLimit),
    sizeof(EncodedDispatchResult), alignof(EncodedDispatchResult),
    offsetof(EncodedDispatchResult, status), offsetof(EncodedDispatchResult, written),
    offsetof(EncodedDispatchResult, wire)>;
} // namespace detail

// TBM1 is independently admitted; TRM1 never authorizes mutations. The owner
// guarantees the capability fingerprint describes model for admittedSession.
// Non-Batch operations keep the legacy dispatch contract; ReadMany still needs
// its separate capability/overload. No queue, replay, rollback or deduplication
// is added here. Retrying a complete request may repeat every endpoint call.
// Request/response, Workspace, capability and referenced Model/control storage
// have the same disjointness/lifetime requirements as the existing dispatcher.
[[nodiscard]] EncodedDispatchResult dispatchEncoded(
    ::telemetry::ModelView model, ::telemetry::Workspace& workspace, std::uint32_t admittedSession,
    std::span<const std::byte> request, std::span<std::byte> response,
    const ::telemetry::protocol::encoded::batch::Capability& batch,
    ::telemetry::detail::CurrentStructuredAbiTag, detail::CurrentBatchDispatchLayoutTag) noexcept;

[[nodiscard]] inline EncodedDispatchResult
dispatchEncoded(::telemetry::ModelView model, ::telemetry::Workspace& workspace,
                std::uint32_t admittedSession, std::span<const std::byte> request,
                std::span<std::byte> response,
                const ::telemetry::protocol::encoded::batch::Capability& batch) noexcept
{
	return dispatchEncoded(model, workspace, admittedSession, request, response, batch,
	                       ::telemetry::detail::CurrentStructuredAbiTag{},
	                       detail::CurrentBatchDispatchLayoutTag{});
}
} // namespace telemetry::integration::encoded
#endif // TELEMETRY_INTEGRATION_ENCODED_BATCH_DISPATCHER_HPP
