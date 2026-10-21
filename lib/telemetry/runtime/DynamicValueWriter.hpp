/*
 * @file DynamicValueWriter.hpp
 * @brief Transactional edits of canonical wire values in caller-owned staging.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#ifndef TELEMETRY_RUNTIME_DYNAMIC_VALUE_WRITER_HPP
#define TELEMETRY_RUNTIME_DYNAMIC_VALUE_WRITER_HPP
#pragma once

#include "DynamicValueView.hpp"

#include <functional>
#include <utility>

namespace telemetry::runtime {

namespace writer_detail {

[[nodiscard]] inline bool overlaps(Input a, Input b) noexcept
{
	if (a.empty() || b.empty())
		return false;
	const std::less<const void*> less{};
	return less(a.data(), b.data() + b.size()) && less(b.data(), a.data() + a.size());
}

template<class T>
[[nodiscard]] Input objectBytes(const T& value) noexcept
{
	return std::as_bytes(std::span{&value, std::size_t{1}});
}

// Check the descriptor, workspace control block and every published index. Only
// addresses/extents are inspected here; names and payloads are not traversed.
// The schema has already been checked live by the caller.
[[nodiscard]] inline bool overlapsSchema(const RuntimeSchema& schema, Input bytes) noexcept
{
	if (overlaps(bytes, objectBytes(*schema.owner())) || overlaps(bytes, schema.descriptor()))
		return true;
	for (TypeId id = 0; const auto* type = schema.type(id); ++id) {
		if (overlaps(bytes, objectBytes(*type)) ||
		    overlaps(bytes, std::as_bytes(schema.members(id))) ||
		    overlaps(bytes, std::as_bytes(schema.enumEntries(id))))
			return true;
	}
	for (const auto category : {descriptor::Category::Field, descriptor::Category::Command,
	                            descriptor::Category::Service}) {
		if (overlaps(bytes, std::as_bytes(schema.catalogs(category))) ||
		    overlaps(bytes, std::as_bytes(schema.endpoints(category))))
			return true;
	}
	return false;
}

// Copy the representation into an integer before touching staging. In
// particular no floating-point operation or by-value FP parameter is involved.
template<class T>
[[nodiscard]] std::uint64_t scalarBits(const T& value) noexcept
{
	if constexpr (std::is_same_v<T, bool>)
		return value ? 1 : 0;
	else if constexpr (std::is_same_v<T, float>)
		return std::bit_cast<std::uint32_t>(value);
	else if constexpr (std::is_same_v<T, double>)
		return std::bit_cast<std::uint64_t>(value);
	else
		return std::bit_cast<std::make_unsigned_t<T>>(value);
}

} // namespace writer_detail

// One editor borrows an exclusive scratch span and a schema workspace. Source
// bytes need live only through begin(); destination may alias that source.
// Scratch must stay disjoint from source, destination, live schema storage and
// the editor object. Keep all borrowed storage alive until completion, or first
// invalidate the schema while retaining its workspace control object. No
// concurrent mutation of scratch, schema or destination is supported.
//
// begin() and commit() validate the complete DTO once. Prepared scalar sets do
// constant identity/type/extent checks and never rescan the DTO. Every failed
// operation is sticky: it leaves staging/destination unchanged for that
// operation and prevents subsequent commit. A successful commit consumes the
// editor. This is logical transactionality, not atomic publication to threads
// or DMA readers.
// Public methods: begin(), status()/bool, exact set()/setEnumUnderlying(),
// commit(), move construction/assignment. Copying an editing lease is forbidden.
class DynamicValueWriter {
public:
	DynamicValueWriter() noexcept = default;
	DynamicValueWriter(const DynamicValueWriter&) = delete;
	DynamicValueWriter& operator=(const DynamicValueWriter&) = delete;

	DynamicValueWriter(DynamicValueWriter&& other) noexcept
	{
		*this = std::move(other);
	}

	DynamicValueWriter& operator=(DynamicValueWriter&& other) noexcept
	{
		if (this != &other) {
			schema_ = other.schema_;
			scratch_ = other.scratch_;
			root_ = other.root_;
			bytes_ = other.bytes_;
			status_ = other.status_;
			other.schema_ = {};
			other.scratch_ = {};
			other.status_ = Status::Stale;
		}
		return *this;
	}

	[[nodiscard]] static DynamicValueWriter begin(const RuntimeSchema& schema, TypeId root,
	                                              Input original,
	                                              std::span<std::byte> scratch) noexcept
	{
		DynamicValueWriter editor;
		// A stale schema is rejected before looking at any other borrowed bytes.
		const auto checked = schema.view(root, original);
		if (!checked) {
			editor.status_ = checked.status;
			return editor;
		}
		if (scratch.size() < original.size()) {
			editor.status_ = Status::Capacity;
			return editor;
		}
		if (writer_detail::overlaps(scratch, original) ||
		    writer_detail::overlaps(scratch, writer_detail::objectBytes(schema)) ||
		    writer_detail::overlaps(scratch, writer_detail::objectBytes(editor)) ||
		    writer_detail::overlapsSchema(schema, scratch)) {
			editor.status_ = Status::Overlap;
			return editor;
		}
		editor.schema_ = schema;
		editor.root_ = root;
		editor.bytes_ = original.size();
		editor.scratch_ = scratch;
		editor.status_ = Status::Ok;
		if (!original.empty())
			std::memcpy(scratch.data(), original.data(), original.size());
		return editor;
	}

	[[nodiscard]] Status status() const noexcept
	{
		if (status_ != Status::Ok)
			return status_;
		return schema_.valid() ? Status::Ok : Status::Stale;
	}

	[[nodiscard]] explicit operator bool() const noexcept
	{
		return status() == Status::Ok;
	}

	template<class T>
	[[nodiscard]] Status set(const PreparedHandle& handle, const T& value) noexcept
	{
		return setScalar(handle, value, false);
	}

	template<class T>
	[[nodiscard]] Status setEnumUnderlying(const PreparedHandle& handle, const T& value) noexcept
	{
		return setScalar(handle, value, true);
	}

	[[nodiscard]] Result<std::size_t> commit(std::span<std::byte> output) noexcept
	{
		const auto current = status();
		if (current != Status::Ok)
			return {fail(current), 0};
		if (output.size() != bytes_)
			return {fail(Status::InvalidPayload), 0};
		if (writer_detail::overlaps(output, scratch_) ||
		    writer_detail::overlaps(output, writer_detail::objectBytes(*this)) ||
		    writer_detail::overlaps(scratch_, writer_detail::objectBytes(*this)) ||
		    writer_detail::overlapsSchema(schema_, output))
			return {fail(Status::Overlap), 0};
		const auto checked = schema_.view(root_, scratch_.first(bytes_));
		if (!checked)
			return {fail(checked.status), 0};
		if (bytes_ != 0)
			std::memcpy(output.data(), scratch_.data(), bytes_);
		status_ = Status::Stale;
		return {Status::Ok, bytes_};
	}

private:
	[[nodiscard]] Status fail(Status failure) noexcept
	{
		if (status_ == Status::Ok)
			status_ = failure;
		return status_;
	}

	template<class T>
	[[nodiscard]] Status setScalar(const PreparedHandle& handle, const T& value,
	                               bool enumeration) noexcept
	{
		static_assert(value_detail::scalar<T>, "Write the exact fixed-width scalar C++ type");
		const auto current = status();
		if (current != Status::Ok)
			return fail(current);
		if (!schema_.accepts(handle))
			return fail(Status::Stale);
		if (handle.rootType() != root_)
			return fail(Status::TypeMismatch);
		const auto* type = schema_.type(handle.leafType());
		if (enumeration) {
			if (!type || type->kind != TypeKind::Enum)
				return fail(Status::TypeMismatch);
			type = schema_.type(type->relatedTypeId);
		}
		if (!type || type->kind != TypeKind::Scalar ||
		    type->scalarCode != value_detail::scalarCode<T>())
			return fail(Status::TypeMismatch);
		if (!value_detail::range(handle.wireOffset(), type->wireBytes, bytes_))
			return fail(Status::InvalidPayload);
		if (writer_detail::overlaps(scratch_, writer_detail::objectBytes(*this)))
			return fail(Status::Overlap);
		const auto bits = writer_detail::scalarBits(value);
		for (std::size_t i = 0; i < type->wireBytes; ++i)
			scratch_[handle.wireOffset() + i] = std::byte((bits >> (i * 8)) & 0xffu);
		return Status::Ok;
	}

	RuntimeSchema schema_{};
	std::span<std::byte> scratch_{};
	TypeId root_ = 0;
	std::size_t bytes_ = 0;
	Status status_ = Status::Stale;
};

} // namespace telemetry::runtime
#endif // TELEMETRY_RUNTIME_DYNAMIC_VALUE_WRITER_HPP
