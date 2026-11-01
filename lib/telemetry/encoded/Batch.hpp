/*
 * @file Batch.hpp
 * @brief Bounded ordered mixed endpoint execution without transport or session state.
 * @author Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT
 */
#ifndef TELEMETRY_ENCODED_BATCH_HPP
#define TELEMETRY_ENCODED_BATCH_HPP
#pragma once

#include <telemetry/encoded/Dispatcher.hpp>

namespace telemetry {
inline constexpr std::size_t batchMaxItems = 32;
inline constexpr std::size_t batchMaxRequestBytes = 65535, batchMaxResponseBytes = 65535;

struct BatchEncodedItem {
	Operation operation = Operation::FieldRead;
	PackedId id = 0;
	std::span<const std::byte> payload{};
};

struct BatchEncodedResult {
	DispatchStatus dispatch = DispatchStatus::InvalidPayload;
	std::uint32_t written = 0;
};

namespace detail {
template<std::size_t... Parts>
struct BatchEncodedLayoutTag {};

using CurrentBatchEncodedLayoutTag =
    BatchEncodedLayoutTag<sizeof(BatchEncodedItem), alignof(BatchEncodedItem),
                          offsetof(BatchEncodedItem, operation), offsetof(BatchEncodedItem, id),
                          offsetof(BatchEncodedItem, payload), sizeof(BatchEncodedResult),
                          alignof(BatchEncodedResult), offsetof(BatchEncodedResult, dispatch),
                          offsetof(BatchEncodedResult, written)>;
} // namespace detail

// Every item ID, declared write capability, exact input, canonical representation,
// maximum response and scratch bound is checked before the first endpoint. A
// global failure leaves output unchanged. A read-only FieldWrite is a global
// InvalidPayload refusal. Callback/application failures are per-item outcomes;
// following items still execute in order. Duplicate IDs deliberately remain.
// No atomic snapshot, rollback or duplicate suppression is promised.
// Model metadata/owners, items, payloads and Workspace stay live and externally
// serialized for this synchronous call. Output, items and payloads are disjoint
// from Workspace; output is disjoint from items and payloads. Writable storage
// must additionally be disjoint from Model backing storage, endpoint owners and
// control objects, as for the existing encoded APIs. Reported item payloads only
// include successful canonical values; failed callback bytes are never published.
[[nodiscard]] BatchEncodedResult batchEncoded(ModelView model,
                                              std::span<const BatchEncodedItem> items,
                                              std::span<std::byte> output, Workspace& workspace,
                                              detail::CurrentStructuredAbiTag,
                                              detail::CurrentBatchEncodedLayoutTag) noexcept;

[[nodiscard]] inline BatchEncodedResult batchEncoded(ModelView model,
                                                     std::span<const BatchEncodedItem> items,
                                                     std::span<std::byte> output,
                                                     Workspace& workspace) noexcept
{
	return batchEncoded(model, items, output, workspace, detail::CurrentStructuredAbiTag{},
	                    detail::CurrentBatchEncodedLayoutTag{});
}
} // namespace telemetry
#endif // TELEMETRY_ENCODED_BATCH_HPP
