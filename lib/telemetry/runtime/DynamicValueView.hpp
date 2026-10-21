/*
 * @file DynamicValueView.hpp
 * @brief Checked non-owning reads of canonical values using runtime metadata.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#ifndef TELEMETRY_RUNTIME_DYNAMIC_VALUE_VIEW_HPP
#define TELEMETRY_RUNTIME_DYNAMIC_VALUE_VIEW_HPP
#pragma once

#include "RuntimeSchema.hpp"

#include <bit>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <string_view>
#include <type_traits>

namespace telemetry::runtime {

// An open enum may contain an unnamed underlying integer. The known flag makes
// this successful numeric decode explicit without inventing a dictionary name.
struct EnumName {
	bool known = false;
	std::string_view name{};
};

namespace value_detail {

template<class T>
inline constexpr bool scalar =
    std::is_same_v<T, bool> || std::is_same_v<T, std::uint8_t> || std::is_same_v<T, std::int8_t> ||
    std::is_same_v<T, std::uint16_t> || std::is_same_v<T, std::int16_t> ||
    std::is_same_v<T, std::uint32_t> || std::is_same_v<T, std::int32_t> ||
    std::is_same_v<T, std::uint64_t> || std::is_same_v<T, std::int64_t> ||
    std::is_same_v<T, float> || std::is_same_v<T, double>;

template<class T>
[[nodiscard]] constexpr ScalarCode scalarCode() noexcept
{
	static_assert(scalar<T>,
	              "Read the exact fixed-width scalar type; no numeric conversion is performed");
	if constexpr (std::is_same_v<T, bool>)
		return ScalarCode::Bool;
	else if constexpr (std::is_same_v<T, float>)
		return ScalarCode::F32;
	else if constexpr (std::is_same_v<T, double>)
		return ScalarCode::F64;
	else if constexpr (sizeof(T) == 1)
		return std::is_signed_v<T> ? ScalarCode::S8 : ScalarCode::U8;
	else if constexpr (sizeof(T) == 2)
		return std::is_signed_v<T> ? ScalarCode::S16 : ScalarCode::U16;
	else if constexpr (sizeof(T) == 4)
		return std::is_signed_v<T> ? ScalarCode::S32 : ScalarCode::U32;
	else
		return std::is_signed_v<T> ? ScalarCode::S64 : ScalarCode::U64;
}

[[nodiscard]] constexpr bool range(std::size_t offset, std::size_t size, std::size_t total) noexcept
{
	return offset <= total && size <= total - offset;
}

// The caller has checked the extent. memcpy handles unaligned input without
// starting an integer object's lifetime in the borrowed payload.
template<class U>
[[nodiscard]] U integer(Input bytes, std::uint32_t offset) noexcept
{
	static_assert(std::is_unsigned_v<U> && !std::is_same_v<U, bool>);
	if constexpr (std::endian::native == std::endian::little) {
		U value;
		std::memcpy(&value, bytes.data() + offset, sizeof(U));
		return value;
	} else {
		std::uint64_t bits = 0;
		for (std::size_t i = 0; i < sizeof(U); ++i)
			bits |= std::uint64_t{std::to_integer<std::uint8_t>(bytes[offset + i])} << (8 * i);
		return static_cast<U>(bits);
	}
}

[[nodiscard]] inline std::uint64_t bits(Input bytes, std::uint32_t offset,
                                        std::uint32_t width) noexcept
{
	std::uint64_t result = 0;
	for (std::uint32_t i = 0; i < width; ++i)
		result |= std::uint64_t{std::to_integer<std::uint8_t>(bytes[offset + i])} << (8 * i);
	return result;
}

template<class T>
[[nodiscard]] T read(Input bytes, std::uint32_t offset) noexcept
{
	if constexpr (std::is_same_v<T, bool>)
		return std::to_integer<std::uint8_t>(bytes[offset]) != 0;
	else if constexpr (std::is_same_v<T, float>)
		return std::bit_cast<float>(integer<std::uint32_t>(bytes, offset));
	else if constexpr (std::is_same_v<T, double>)
		return std::bit_cast<double>(integer<std::uint64_t>(bytes, offset));
	else {
		using U = std::make_unsigned_t<T>;
		if constexpr (std::is_signed_v<T>)
			return std::bit_cast<T>(integer<U>(bytes, offset));
		else
			return integer<U>(bytes, offset);
	}
}

// Metadata has already passed the schema parser. Keep independent traversal
// budgets here so value validation cannot exceed the fixed wire limits.
[[nodiscard]] inline Status validate(const RuntimeSchema& schema, TypeId id, Input bytes,
                                     std::uint32_t offset, std::uint32_t depth,
                                     std::uint32_t& visited) noexcept
{
	if (depth > Limits::maxTypeDepth || visited >= Limits::maxExpandedValueNodes)
		return Status::InvalidPayload;
	++visited;
	const auto* type = schema.type(id);
	if (!type || !range(offset, type->wireBytes, bytes.size()))
		return Status::InvalidPayload;
	if (type->kind == TypeKind::Scalar) {
		if (type->scalarCode == ScalarCode::Bool &&
		    std::to_integer<std::uint8_t>(bytes[offset]) > 1)
			return Status::InvalidPayload;
		return Status::Ok;
	}
	if (type->kind == TypeKind::Enum)
		return Status::Ok; // Every underlying integer is a valid open enum value.
	if (type->kind == TypeKind::Struct) {
		for (const auto& member : schema.members(id)) {
			const auto wideOffset = std::uint64_t{offset} + member.wireOffset;
			if (wideOffset > bytes.size())
				return Status::InvalidPayload;
			const auto status =
			    validate(schema, member.typeId, bytes, static_cast<std::uint32_t>(wideOffset),
			             depth + 1, visited);
			if (status != Status::Ok)
				return status;
		}
		return Status::Ok;
	}
	if (type->kind == TypeKind::Array) {
		const auto* element = schema.type(type->relatedTypeId);
		if (!element)
			return Status::InvalidPayload;
		// A zero-wire subtree cannot contain a bool or another scalar. Its
		// structural node/depth budgets were checked once by the parser.
		if (element->wireBytes == 0)
			return Status::Ok;
		for (std::uint32_t i = 0; i < type->elementCount; ++i) {
			const auto wideOffset = std::uint64_t{offset} + std::uint64_t{i} * element->wireBytes;
			if (wideOffset > bytes.size())
				return Status::InvalidPayload;
			const auto status =
			    validate(schema, element->id, bytes, static_cast<std::uint32_t>(wideOffset),
			             depth + 1, visited);
			if (status != Status::Ok)
				return status;
		}
		return Status::Ok;
	}
	return Status::TypeMismatch;
}

} // namespace value_detail

// This object borrows both a schema workspace and one immutable encoded value.
// The workspace, its metadata storage, descriptor bytes and payload must outlive
// every view/result using them. No concurrent mutation is supported. reset() or
// a new parse invalidates old views/handles before payload access, provided the
// workspace itself is still alive. Arbitrary destruction/free cannot be detected.
//
// Creating a root view checks its complete representation. Navigation borrows a
// subtree without constructing an aggregate. Prepared reads do constant direct
// identity/type/extent checks; they perform no path lookup or value traversal.
// Public methods:
// - create()/valid(): Validate a borrowed root value and inspect schema liveness.
// - typeId()/rootTypeId()/wireOffset(): Inspect stored identifiers and position.
// - bytes()/member()/index(): Access checked borrowed bytes or navigate children.
// - get()/enumUnderlying()/enumName(): Read exact scalars and open enum values.
class DynamicValueView {
public:
	DynamicValueView() noexcept = default;

	[[nodiscard]] static Result<DynamicValueView> create(const RuntimeSchema& schema, TypeId root,
	                                                     Input bytes) noexcept
	{
		static_assert(CHAR_BIT == 8);
		static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559 &&
		              std::numeric_limits<float>::radix == 2 &&
		              std::numeric_limits<float>::digits == 24);
		static_assert(sizeof(double) == 8 && std::numeric_limits<double>::is_iec559 &&
		              std::numeric_limits<double>::radix == 2 &&
		              std::numeric_limits<double>::digits == 53);
		if (!schema.valid())
			return {Status::Stale, {}};
		const auto* type = schema.type(root);
		if (!type)
			return {Status::TypeNotFound, {}};
		if (type->kind == TypeKind::Void)
			return {Status::TypeMismatch, {}};
		if (type->wireBytes != bytes.size() || type->depth > Limits::maxTypeDepth ||
		    type->expandedNodes > Limits::maxExpandedValueNodes)
			return {Status::InvalidPayload, {}};
		std::uint32_t visited = 0;
		const auto checked = value_detail::validate(schema, root, bytes, 0, 0, visited);
		if (checked != Status::Ok)
			return {checked, {}};
		return {Status::Ok, DynamicValueView{schema, root, root, bytes, 0}};
	}

	[[nodiscard]] bool valid() const noexcept
	{
		return schema_.valid();
	}

	[[nodiscard]] TypeId typeId() const noexcept
	{
		return type_;
	}

	[[nodiscard]] TypeId rootTypeId() const noexcept
	{
		return root_;
	}

	[[nodiscard]] std::uint32_t wireOffset() const noexcept
	{
		return offset_;
	}

	[[nodiscard]] Result<Input> bytes() const noexcept
	{
		const auto checked = current();
		if (!checked)
			return {checked.status, {}};
		return {Status::Ok, bytes_.subspan(offset_, checked.value->wireBytes)};
	}

	[[nodiscard]] Result<DynamicValueView> member(std::string_view name) const noexcept
	{
		const auto checked = current();
		if (!checked)
			return {checked.status, {}};
		if (checked.value->kind != TypeKind::Struct)
			return {Status::TypeMismatch, {}};
		for (const auto& item : schema_.members(type_)) {
			if (item.name == name)
				return child(item.typeId, item.wireOffset);
		}
		return {Status::MemberNotFound, {}};
	}

	[[nodiscard]] Result<DynamicValueView> member(std::uint32_t index) const noexcept
	{
		const auto checked = current();
		if (!checked)
			return {checked.status, {}};
		if (checked.value->kind != TypeKind::Struct)
			return {Status::TypeMismatch, {}};
		const auto members = schema_.members(type_);
		if (index >= members.size())
			return {Status::IndexOutOfRange, {}};
		return child(members[index].typeId, members[index].wireOffset);
	}

	[[nodiscard]] Result<DynamicValueView> index(std::uint32_t index) const noexcept
	{
		const auto checked = current();
		if (!checked)
			return {checked.status, {}};
		if (checked.value->kind != TypeKind::Array)
			return {Status::TypeMismatch, {}};
		if (index >= checked.value->elementCount)
			return {Status::IndexOutOfRange, {}};
		const auto* element = schema_.type(checked.value->relatedTypeId);
		if (!element)
			return {Status::TypeNotFound, {}};
		const auto relative = std::uint64_t{index} * element->wireBytes;
		if (relative > std::numeric_limits<std::uint32_t>::max())
			return {Status::InvalidPayload, {}};
		return child(element->id, static_cast<std::uint32_t>(relative));
	}

	template<class T>
	[[nodiscard]] Result<T> get() const noexcept
	{
		return scalar<T>(current(), offset_, false);
	}

	template<class T>
	[[nodiscard]] Result<T> get(const PreparedHandle& handle) const noexcept
	{
		return scalar<T>(prepared(handle), handle.wireOffset(), false);
	}

	template<class T>
	[[nodiscard]] Result<T> enumUnderlying() const noexcept
	{
		return scalar<T>(current(), offset_, true);
	}

	template<class T>
	[[nodiscard]] Result<T> enumUnderlying(const PreparedHandle& handle) const noexcept
	{
		return scalar<T>(prepared(handle), handle.wireOffset(), true);
	}

	[[nodiscard]] Result<EnumName> enumName() const noexcept
	{
		return enumName(current(), offset_);
	}

	[[nodiscard]] Result<EnumName> enumName(const PreparedHandle& handle) const noexcept
	{
		return enumName(prepared(handle), handle.wireOffset());
	}

private:
	DynamicValueView(const RuntimeSchema& schema, TypeId root, TypeId type, Input bytes,
	                 std::uint32_t offset) noexcept
	    : schema_(schema), bytes_(bytes), root_(root), type_(type), offset_(offset)
	{}

	[[nodiscard]] Result<const RuntimeType*> current() const noexcept
	{
		if (!schema_.valid())
			return {Status::Stale, nullptr};
		const auto* type = schema_.type(type_);
		if (!type)
			return {Status::TypeNotFound, nullptr};
		if (!value_detail::range(offset_, type->wireBytes, bytes_.size()))
			return {Status::InvalidPayload, nullptr};
		return {Status::Ok, type};
	}

	[[nodiscard]] Result<const RuntimeType*> prepared(const PreparedHandle& handle) const noexcept
	{
		const auto checked = current();
		if (!checked)
			return checked;
		if (!schema_.accepts(handle))
			return {Status::Stale, nullptr};
		if (handle.rootType() != root_)
			return {Status::TypeMismatch, nullptr};
		const auto* leaf = schema_.type(handle.leafType());
		if (!leaf || handle.wireOffset() < offset_ ||
		    !value_detail::range(handle.wireOffset() - offset_, leaf->wireBytes,
		                         checked.value->wireBytes) ||
		    !value_detail::range(handle.wireOffset(), leaf->wireBytes, bytes_.size()))
			return {Status::InvalidPayload, nullptr};
		return {Status::Ok, leaf};
	}

	[[nodiscard]] Result<DynamicValueView> child(TypeId type, std::uint32_t relative) const noexcept
	{
		const auto checked = current();
		if (!checked)
			return {checked.status, {}};
		const auto* item = schema_.type(type);
		if (!item || !value_detail::range(relative, item->wireBytes, checked.value->wireBytes))
			return {Status::InvalidPayload, {}};
		const auto absolute = std::uint64_t{offset_} + relative;
		if (absolute > std::numeric_limits<std::uint32_t>::max() ||
		    !value_detail::range(static_cast<std::size_t>(absolute), item->wireBytes,
		                         bytes_.size()))
			return {Status::InvalidPayload, {}};
		return {Status::Ok, DynamicValueView{schema_, root_, type, bytes_,
		                                     static_cast<std::uint32_t>(absolute)}};
	}

	template<class T>
	[[nodiscard]] Result<T> scalar(Result<const RuntimeType*> checked, std::uint32_t offset,
	                               bool enumeration) const noexcept
	{
		static_assert(value_detail::scalar<T>, "Read the exact fixed-width scalar C++ type");
		if (!checked)
			return {checked.status, {}};
		const RuntimeType* type = checked.value;
		if (enumeration) {
			if (type->kind != TypeKind::Enum)
				return {Status::TypeMismatch, {}};
			type = schema_.type(type->relatedTypeId);
		}
		if (!type || type->kind != TypeKind::Scalar ||
		    type->scalarCode != value_detail::scalarCode<T>() ||
		    !value_detail::range(offset, type->wireBytes, bytes_.size()))
			return {Status::TypeMismatch, {}};
		if constexpr (std::is_same_v<T, bool>) {
			if (std::to_integer<std::uint8_t>(bytes_[offset]) > 1)
				return {Status::InvalidPayload, {}};
		}
		return {Status::Ok, value_detail::read<T>(bytes_, offset)};
	}

	[[nodiscard]] Result<EnumName> enumName(Result<const RuntimeType*> checked,
	                                        std::uint32_t offset) const noexcept
	{
		if (!checked)
			return {checked.status, {}};
		if (checked.value->kind != TypeKind::Enum)
			return {Status::TypeMismatch, {}};
		const auto* underlying = schema_.type(checked.value->relatedTypeId);
		if (!underlying || underlying->wireBytes == 0 || underlying->wireBytes > 8 ||
		    !value_detail::range(offset, underlying->wireBytes, bytes_.size()))
			return {Status::InvalidPayload, {}};
		const auto code = value_detail::bits(bytes_, offset, underlying->wireBytes);
		for (const auto& item : schema_.enumEntries(checked.value->id)) {
			if (item.codeBits == code)
				return {Status::Ok, {true, item.name}};
		}
		return {Status::Ok, {false, {}}};
	}

	RuntimeSchema schema_{};
	Input bytes_{};
	TypeId root_ = 0;
	TypeId type_ = 0;
	std::uint32_t offset_ = 0;
};

[[nodiscard]] inline Result<DynamicValueView> RuntimeSchema::view(TypeId root,
                                                                  Input bytes) const noexcept
{
	return DynamicValueView::create(*this, root, bytes);
}

} // namespace telemetry::runtime
#endif // TELEMETRY_RUNTIME_DYNAMIC_VALUE_VIEW_HPP
