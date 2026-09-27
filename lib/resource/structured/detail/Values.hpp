/*
 * @file Values.hpp
 * @brief Indexed values stream and its exact compiled-boundary layout tag.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#ifndef RESOURCE_STRUCTURED_DETAIL_VALUES_HPP
#define RESOURCE_STRUCTURED_DETAIL_VALUES_HPP

#include "../../Types.hpp"
#include <telemetry_structured/abi/StructuredAbi.hpp>

namespace resource::structured::detail {

// The final entry is an EOF sentinel. Packed IDs remain u32; only the generic
// resource cursor and the cached descriptor fingerprint use u64 on the wire.
struct ValueToken {
    std::uint32_t offset;
    telemetry::PackedId id;
};

struct ValuesView {
    telemetry::structured::FieldIndex fields;
    telemetry::structured::Workspace* workspace;
    std::uint64_t fingerprint;
    std::uint32_t count;
    std::uint32_t totalBytes;
    std::uint32_t maxScratch;
};

static_assert(std::is_standard_layout_v<ValuesView> &&
              std::is_trivially_copyable_v<ValuesView> &&
              std::is_standard_layout_v<ValueToken> &&
              std::is_standard_layout_v<ReadResult>);

template <class CoreTag, std::size_t... Parts> struct ValuesAbiTag {};
using CurrentValuesAbiTag = ValuesAbiTag<telemetry::structured::detail::CurrentStructuredAbiTag,
    1, sizeof(ValueToken), alignof(ValueToken), offsetof(ValueToken, offset), offsetof(ValueToken, id),
    sizeof(ValuesView), alignof(ValuesView), offsetof(ValuesView, fields), offsetof(ValuesView, workspace),
    offsetof(ValuesView, fingerprint), offsetof(ValuesView, count), offsetof(ValuesView, totalBytes),
    offsetof(ValuesView, maxScratch), sizeof(ReadResult), alignof(ReadResult),
    offsetof(ReadResult, next), offsetof(ReadResult, written), offsetof(ReadResult, status),
    offsetof(ReadResult, eof)>;

// One checked boundary: validate the cursor, output capacity and workspace
// overlap before handing raw pointers to the existing Field read thunks.
[[nodiscard]] ReadResult readValues(const ValuesView&, const ValueToken*, Cursor, Output,
                                    CurrentValuesAbiTag) noexcept;

} // namespace resource::structured::detail
#endif
