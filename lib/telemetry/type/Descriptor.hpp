/*
 * @file Descriptor.hpp
 * @brief Immutable, non-owning views of structural type metadata.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */

/*
 * Immutable structural metadata views produced by the complete type registry.
 *
 * Descriptors borrow static member and enum arrays and contain no live endpoint
 * value or owner address. TypeIds are positions within one registry. Related
 * types and kind-specific fields let descriptor writers walk the structure
 * without knowing the original C++ template arguments.
 */

#ifndef TELEMETRY_TYPE_DESCRIPTOR_HPP
#define TELEMETRY_TYPE_DESCRIPTOR_HPP
#pragma once

#include "Traits.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace telemetry {

using TypeId = std::uint32_t;

// One aggregate member in declaration order. Its name and referenced type
// describe structure; native byte offsets and live member values are absent.
struct MemberDescriptor {
	TypeId typeId = 0;
	std::string_view name{};
};

// Underlying code bits and name of one enum entry.
struct EnumEntryDescriptor {
	// The low wireBytes bytes are the enum's unsigned underlying bit pattern.
	// This also preserves negative signed codes without a numeric conversion.
	std::uint64_t codeBits = 0;
	std::string_view name{};
};

// Kind determines which auxiliary fields are meaningful. Members/enum entries
// borrow immutable registry-owned arrays; accessors reject out-of-range indexes.
// wireBytes excludes native padding, while recordBytes includes wire metadata.
// Public methods:
// - member(): Borrow checked member.
// - enumEntry(): Borrow checked entry.
struct TypeDescriptor {
	TypeId id = 0;
	TypeKind kind = TypeKind::Void;
	std::uint32_t wireBytes = 0;
	std::uint32_t recordBytes = 0; // Includes the v3 record header.

	// Only Scalar uses scalarCode. Enum/Array use relatedTypeId for their
	// underlying/element type. Array alone uses elementCount.
	ScalarCode scalarCode = static_cast<ScalarCode>(0);
	TypeId relatedTypeId = 0;
	std::uint32_t elementCount = 0;

	const MemberDescriptor* memberData = nullptr;
	std::uint32_t memberCount = 0;
	const EnumEntryDescriptor* enumData = nullptr;
	std::uint32_t enumCount = 0;

	[[nodiscard]] constexpr const MemberDescriptor* member(std::size_t index) const noexcept
	{
		return memberData != nullptr && index < memberCount ? memberData + index : nullptr;
	}

	[[nodiscard]] constexpr const EnumEntryDescriptor* enumEntry(std::size_t index) const noexcept
	{
		return enumData != nullptr && index < enumCount ? enumData + index : nullptr;
	}
};

// Borrowed descriptor array from one registry. TypeId is a checked positional
// index into data, not a globally assigned or persistent type identity.
// Public methods:
// - find(): Find checked type.
struct TypeRegistryView {
	const TypeDescriptor* data = nullptr;
	std::uint32_t count = 0;
	std::uint32_t recordsBytes = 0;

	[[nodiscard]] constexpr const TypeDescriptor* find(TypeId id) const noexcept
	{
		return data != nullptr && id < count ? data + id : nullptr;
	}
};

} // namespace telemetry

#endif
