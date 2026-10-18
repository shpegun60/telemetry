/*
 * @file Dispatcher.hpp
 * @brief Synchronous, transport-independent encoded endpoint dispatch.
 * @author Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT
 */
#ifndef TELEMETRY_ENCODED_DISPATCHER_HPP
#define TELEMETRY_ENCODED_DISPATCHER_HPP
#pragma once

#include <telemetry/abi/StructuredAbi.hpp>
#include <telemetry/core/Operation.hpp>
#include <cstddef>
#include <cstdint>
#include <span>

namespace telemetry {
// Dispatch and application status are independent. endpointStatus is meaningful
// only when dispatch is Ok and is interpreted using operation: WriteResult,
// CommandResult, ServiceStatus, or zero for FieldRead. Accepted is an application
// outcome, not evidence of eventual completion. written counts response bytes
// for successful FieldRead/Service calls; every other outcome reports zero.
struct EncodedDispatchResult {
	Operation operation = Operation::FieldRead;
	DispatchStatus dispatch = DispatchStatus::InternalError;
	std::uint8_t endpointStatus = 0;
	std::uint32_t written = 0;
};

namespace detail {
// Preserve the existing four compiled APIs' exact ABI tag. This additional
// result layout participates only in the new compiled dispatch symbol.
template<std::size_t... Parts>
struct EncodedDispatchLayoutTag {};

using CurrentEncodedDispatchLayoutTag = EncodedDispatchLayoutTag<
    sizeof(EncodedDispatchResult), alignof(EncodedDispatchResult),
    offsetof(EncodedDispatchResult, operation), offsetof(EncodedDispatchResult, dispatch),
    offsetof(EncodedDispatchResult, endpointStatus), offsetof(EncodedDispatchResult, written)>;
} // namespace detail

// Model, endpoint owners, byte spans and Workspace are borrowed for this entire
// synchronous call. The owner serializes their access. The existing family
// boundaries keep their length, capacity, scratch and overlap checks before
// invoking callbacks; Service input/output may overlap. Session admission,
// framing, correlation, queues and retry policy belong to the caller.
// FieldRead requires empty input. An undefined operation returns InvalidPayload
// without invoking any endpoint. Other error precedence matches the four APIs.
[[nodiscard]] EncodedDispatchResult
dispatchEncoded(ModelView model, Operation operation, PackedId endpointId,
                std::span<const std::byte> input, std::span<std::byte> output, Workspace& workspace,
                detail::CurrentStructuredAbiTag, detail::CurrentEncodedDispatchLayoutTag) noexcept;

[[nodiscard]] inline EncodedDispatchResult dispatchEncoded(ModelView model, Operation operation,
                                                           PackedId endpointId,
                                                           std::span<const std::byte> input,
                                                           std::span<std::byte> output,
                                                           Workspace& workspace) noexcept
{
	return dispatchEncoded(model, operation, endpointId, input, output, workspace,
	                       detail::CurrentStructuredAbiTag{},
	                       detail::CurrentEncodedDispatchLayoutTag{});
}
} // namespace telemetry

#endif // TELEMETRY_ENCODED_DISPATCHER_HPP
