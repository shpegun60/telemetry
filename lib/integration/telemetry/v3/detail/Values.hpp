/*
 * @file Values.hpp
 * @brief Indexed values stream and its exact compiled-boundary layout tag.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 *
 * Separate the template-owned token index from the compiled read routine.
 * The ABI tag makes incompatible layouts fail at linking instead of allowing
 * a caller and implementation to interpret the same pointers differently.
 */
#ifndef RESOURCE_TELEMETRY_V3_DETAIL_VALUES_HPP
#define RESOURCE_TELEMETRY_V3_DETAIL_VALUES_HPP
#pragma once

#include <resource/Types.hpp>
#include <telemetry/abi/StructuredAbi.hpp>

namespace resource::telemetry::v3::detail {

// The final entry is an EOF sentinel. Packed IDs remain u32; only the generic
// resource cursor and the cached descriptor fingerprint use u64 on the wire.
struct ValueToken {
	std::uint32_t offset;
	::telemetry::PackedId id;
};

struct ValuesView {
	::telemetry::FieldIndex fields;
	::telemetry::Workspace* workspace;
	std::uint64_t fingerprint;
	std::uint32_t count;
	std::uint32_t totalBytes;
	std::uint32_t maxScratch;
};

static_assert(std::is_standard_layout_v<ValuesView> && std::is_trivially_copyable_v<ValuesView> &&
              std::is_standard_layout_v<ValueToken> && std::is_standard_layout_v<ReadResult>);

template<class CoreTag, std::size_t... Parts>
struct ValuesAbiTag {};

using CurrentValuesAbiTag =
    ValuesAbiTag<::telemetry::detail::CurrentStructuredAbiTag, 1, sizeof(ValueToken),
                 alignof(ValueToken), offsetof(ValueToken, offset), offsetof(ValueToken, id),
                 sizeof(ValuesView), alignof(ValuesView), offsetof(ValuesView, fields),
                 offsetof(ValuesView, workspace), offsetof(ValuesView, fingerprint),
                 offsetof(ValuesView, count), offsetof(ValuesView, totalBytes),
                 offsetof(ValuesView, maxScratch), sizeof(ReadResult), alignof(ReadResult),
                 offsetof(ReadResult, next), offsetof(ReadResult, written),
                 offsetof(ReadResult, status), offsetof(ReadResult, eof)>;

// One checked boundary: validate the cursor, output capacity and workspace
// overlap before handing raw pointers to the existing Field read thunks.
[[nodiscard]] ReadResult readValues(const ValuesView&, const ValueToken*, Cursor, Output,
                                    CurrentValuesAbiTag) noexcept;

} // namespace resource::telemetry::v3::detail
#endif
