/*
 * @file ReadMany.hpp
 * @brief Bounded sequential scalar Field reads without transport or session state.
 * @author Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT
 */
#ifndef TELEMETRY_ENCODED_READ_MANY_HPP
#define TELEMETRY_ENCODED_READ_MANY_HPP
#pragma once

#include <telemetry/abi/StructuredAbi.hpp>
#include <cstddef>
#include <cstdint>
#include <span>

namespace telemetry {
inline constexpr std::size_t readManyMaxItems = 32;
inline constexpr std::size_t readManyMaxValueBytes = 8;
inline constexpr std::size_t readManyMaxResponseBytes = 514;

// Ordered item outcomes are independent of the global preflight result.
// Failed items have no value bytes, including getters which wrote before failing.
enum class ReadManyItemStatus : std::uint8_t {
	Ok = 0,
	NotFound = 1,
	UnsupportedType = 2,
	Unavailable = 3,
	ReadError = 4
};

struct ReadManyEncodedResult {
	DispatchStatus dispatch = DispatchStatus::InvalidPayload;
	std::uint32_t written = 0;
};

namespace detail {
template<std::size_t... Parts>
struct ReadManyEncodedLayoutTag {};

using CurrentReadManyEncodedLayoutTag =
    ReadManyEncodedLayoutTag<sizeof(ReadManyEncodedResult), alignof(ReadManyEncodedResult),
                             offsetof(ReadManyEncodedResult, dispatch),
                             offsetof(ReadManyEncodedResult, written)>;
} // namespace detail

// The count and ordered records are encoded as canonical little-endian bytes:
// U16 count; then U32 ID, U8 item status, U8 zero, U16 length and scalar bytes.
// This is a batch body only: admission, framing and correlation belong to the
// caller. No atomic snapshot is promised. Unknown/unsupported Fields are items.
// All count/duplicate/overlap/response/scratch checks precede every getter and
// leave output unchanged on global failure. Reads use existing FieldIndex rows.
// Model metadata, owners, ids, Workspace and output remain live and externally
// serialized. Writable output and Workspace must also be disjoint from Model
// backing storage, endpoint owners and control objects; arbitrary native-owner
// aliases are a caller precondition, as with the existing encoded APIs.
[[nodiscard]] ReadManyEncodedResult
readManyEncoded(ModelView model, std::span<const PackedId> ids, std::span<std::byte> output,
                Workspace& workspace, detail::CurrentStructuredAbiTag,
                detail::CurrentReadManyEncodedLayoutTag) noexcept;

[[nodiscard]] inline ReadManyEncodedResult readManyEncoded(ModelView model,
                                                           std::span<const PackedId> ids,
                                                           std::span<std::byte> output,
                                                           Workspace& workspace) noexcept
{
	return readManyEncoded(model, ids, output, workspace, detail::CurrentStructuredAbiTag{},
	                       detail::CurrentReadManyEncodedLayoutTag{});
}
} // namespace telemetry
#endif // TELEMETRY_ENCODED_READ_MANY_HPP
