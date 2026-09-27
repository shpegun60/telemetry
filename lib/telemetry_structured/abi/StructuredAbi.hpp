/*
 * @file StructuredAbi.hpp
 * @brief Exact link-time tag for in-memory structured descriptor views.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */

#ifndef TELEMETRY_STRUCTURED_ABI_HPP
#define TELEMETRY_STRUCTURED_ABI_HPP

#include "../model/Model.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

namespace telemetry::structured {

// This describes the C++ in-memory ABI, not the version of descriptor.bin.
inline constexpr std::uint32_t structuredAbiRevision = 2;

static_assert(std::is_standard_layout_v<MemberDescriptor> &&
              std::is_standard_layout_v<EnumEntryDescriptor> &&
              std::is_standard_layout_v<TypeDescriptor> &&
              std::is_standard_layout_v<TypeRegistryView> &&
              std::is_standard_layout_v<ServiceEntry> &&
              std::is_standard_layout_v<ServiceCatalog> &&
              std::is_standard_layout_v<ServiceIndex> &&
              std::is_standard_layout_v<ServiceTypePair> &&
              std::is_standard_layout_v<ServiceTypeCatalog> &&
              std::is_standard_layout_v<ModelView> &&
              std::is_standard_layout_v<EncodedCallResult> &&
              std::is_trivially_copyable_v<MemberDescriptor> &&
              std::is_trivially_copyable_v<EnumEntryDescriptor> &&
              std::is_trivially_copyable_v<TypeDescriptor> &&
              std::is_trivially_copyable_v<TypeRegistryView> &&
              std::is_trivially_copyable_v<ServiceEntry> &&
              std::is_trivially_copyable_v<ServiceCatalog> &&
              std::is_trivially_copyable_v<ServiceIndex> &&
              std::is_trivially_copyable_v<ServiceTypePair> &&
              std::is_trivially_copyable_v<ServiceTypeCatalog> &&
              std::is_trivially_copyable_v<ModelView> &&
              std::is_trivially_copyable_v<EncodedCallResult>,
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
    offsetof(TypeRegistryView, recordsBytes),
    sizeof(ServiceEntry), alignof(ServiceEntry),
    offsetof(ServiceEntry, definition), offsetof(ServiceEntry, invoke),
    offsetof(ServiceEntry, name), offsetof(ServiceEntry, requestWireBytes),
    offsetof(ServiceEntry, responseWireBytes), offsetof(ServiceEntry, scratchBytes),
    sizeof(ServiceCatalog), alignof(ServiceCatalog),
    offsetof(ServiceCatalog, name), offsetof(ServiceCatalog, entries),
    offsetof(ServiceCatalog, count),
    sizeof(ServiceIndex), alignof(ServiceIndex),
    offsetof(ServiceIndex, catalogs_), offsetof(ServiceIndex, count_),
    sizeof(ServiceTypePair), alignof(ServiceTypePair),
    offsetof(ServiceTypePair, requestTypeId), offsetof(ServiceTypePair, responseTypeId),
    sizeof(ServiceTypeCatalog), alignof(ServiceTypeCatalog),
    offsetof(ServiceTypeCatalog, entries), offsetof(ServiceTypeCatalog, count),
    sizeof(ModelView), alignof(ModelView),
    offsetof(ModelView, types), offsetof(ModelView, services),
    offsetof(ModelView, serviceTypes), offsetof(ModelView, serviceCatalogCount),
    sizeof(EncodedCallResult), alignof(EncodedCallResult),
    offsetof(EncodedCallResult, dispatch), offsetof(EncodedCallResult, endpointStatus),
    offsetof(EncodedCallResult, written),
    sizeof(std::span<const std::byte>), alignof(std::span<const std::byte>),
    sizeof(std::span<std::byte>), alignof(std::span<std::byte>),
    sizeof(Workspace), alignof(Workspace)>;

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
