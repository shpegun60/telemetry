/*
 * @file StructuredAbi.hpp
 * @brief Exact link-time tag for in-memory structured descriptor views.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */

#ifndef TELEMETRY_STRUCTURED_ABI_HPP
#define TELEMETRY_STRUCTURED_ABI_HPP

#include "../type/Descriptor.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

namespace telemetry::structured {

// This describes the C++ in-memory ABI, not the version of descriptor.bin.
inline constexpr std::uint32_t structuredAbiRevision = 1;

static_assert(std::is_standard_layout_v<MemberDescriptor> &&
              std::is_standard_layout_v<EnumEntryDescriptor> &&
              std::is_standard_layout_v<TypeDescriptor> &&
              std::is_standard_layout_v<TypeRegistryView> &&
              std::is_trivially_copyable_v<MemberDescriptor> &&
              std::is_trivially_copyable_v<EnumEntryDescriptor> &&
              std::is_trivially_copyable_v<TypeDescriptor> &&
              std::is_trivially_copyable_v<TypeRegistryView>,
              "Structured ABI views must be standard-layout and trivially copyable");

namespace detail {

template <std::uint64_t... Parts>
struct StructuredAbiTag {};

// Every descriptor field consumed across a translation-unit boundary is in
// this tuple. No hash is used for matching: the entire pack names the symbol.
using CurrentStructuredAbiTag = StructuredAbiTag<
    structuredAbiRevision,
    sizeof(void*), sizeof(std::string_view), alignof(std::string_view),
    sizeof(TypeKind), sizeof(ScalarCode), sizeof(TypeId),
    sizeof(MemberDescriptor), alignof(MemberDescriptor),
    offsetof(MemberDescriptor, typeId), offsetof(MemberDescriptor, name),
    sizeof(EnumEntryDescriptor), alignof(EnumEntryDescriptor),
    offsetof(EnumEntryDescriptor, codeBits), offsetof(EnumEntryDescriptor, name),
    sizeof(TypeDescriptor), alignof(TypeDescriptor),
    offsetof(TypeDescriptor, id), offsetof(TypeDescriptor, kind),
    offsetof(TypeDescriptor, wireBytes), offsetof(TypeDescriptor, recordBytes),
    offsetof(TypeDescriptor, scalarCode), offsetof(TypeDescriptor, relatedTypeId),
    offsetof(TypeDescriptor, elementCount), offsetof(TypeDescriptor, memberData),
    offsetof(TypeDescriptor, memberCount), offsetof(TypeDescriptor, enumData),
    offsetof(TypeDescriptor, enumCount),
    sizeof(TypeRegistryView), alignof(TypeRegistryView),
    offsetof(TypeRegistryView, data), offsetof(TypeRegistryView, count),
    offsetof(TypeRegistryView, recordsBytes)>;

// Only the exact current specialization is defined in StructuredAbi.cpp.
template <class Tag>
void requireStructuredAbi(Tag) noexcept;

template <>
void requireStructuredAbi<CurrentStructuredAbiTag>(CurrentStructuredAbiTag) noexcept;

} // namespace detail

// Call once at a module boundary that exchanges structured descriptor views.
// It has no place in native read/write/service hot paths.
template <class Tag = detail::CurrentStructuredAbiTag>
inline void requireStructuredAbi() noexcept
{
    detail::requireStructuredAbi(Tag{});
}

} // namespace telemetry::structured

#endif
