/*
 * @file FieldTable.hpp
 * @brief One mixed native Field table with bounded encoded read/write.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */

/*
 * Owns heterogeneous Field definitions and the runtime rows derived from them.
 *
 * Typed access keeps exact targets visible. Encoded access checks wire extents
 * and scratch overlap before its typed thunk, with independent read and write
 * storage requirements for borrowed getters. Runtime contexts may refer into
 * the definitions, so a table has a fixed address and cannot move or copy.
 */

#ifndef TELEMETRY_FIELD_FIELD_TABLE_HPP
#define TELEMETRY_FIELD_FIELD_TABLE_HPP
#pragma once

#include "Field.hpp"
#include "../result/EndpointResults.hpp"
#include "../detail/Encoded.hpp"
#include "../detail/Traversal.hpp"
#include "../type/Registry.hpp"
#include <telemetry/core/Id.hpp>
#include <array>

namespace telemetry {

// Borrowed erased row used by runtime/compiled adapters. readEncoded and
// writeEncoded are the checked byte boundaries; raw function members require
// those checks to have completed. Declared write capability stays fixed even
// when its setter slot is empty. Contexts and names must outlive every use.
// Public methods:
// - readEncoded(): Read encoded value.
// - writeEncoded(): Apply encoded value.
struct FieldEntry {
	// Internal operations require the checked boundary below. Their typed
	// bodies already know the exact wire length; no runtime span is passed.
	using Read = EncodedReadResult (*)(const void*, std::byte*, Workspace&) noexcept;
	using Write = EncodedWriteResult (*)(const void*, const std::byte*, Workspace&) noexcept;
	const void* readContext;
	Read read;
	const void* writeContext;
	Write write; // nullptr denotes a read-only definition, not a temporarily empty slot.
	const char* name;
	std::uint32_t wireBytes;
	// A const-reference getter needs no value storage. Its setter can still
	// need a decoded object, so these operation requirements are independent.
	std::uint32_t readScratchBytes;
	std::uint32_t writeScratchBytes;

	[[nodiscard]] EncodedReadResult readEncoded(std::span<std::byte> output,
	                                            Workspace& workspace) const noexcept
	{
		if (output.size() < wireBytes)
			return {DispatchStatus::BufferTooSmall, 0};
		if (readScratchBytes != 0 && buffersOverlap(output, workspace.storage()))
			return {DispatchStatus::InvalidPayload, 0};
		return read(readContext, output.data(), workspace);
	}

	[[nodiscard]] EncodedWriteResult writeEncoded(std::span<const std::byte> input,
	                                              Workspace& workspace) const noexcept
	{
		if (write == nullptr)
			return {DispatchStatus::Ok, telemetry::WriteResult::ReadOnly};
		if (input.size() != wireBytes)
			return {DispatchStatus::InvalidPayload};
		if (writeScratchBytes != 0 && buffersOverlap(input, workspace.storage()))
			return {DispatchStatus::InvalidPayload};
		return write(writeContext, input.data(), workspace);
	}
};

// Owns declarations and derived rows at one stable address. Template positions
// keep the exact binding and value type; runtime access uses erased rows or
// type-specialized conversion dispatch. Borrowing APIs require an lvalue table,
// and the table itself does not retain endpoint values or snapshot owner state.
// Public methods:
// - FieldTable(): Own Field definitions.
// - data(): Borrow runtime rows.
// - size(): Count declared Fields.
// - empty(): Check table emptiness.
// - begin(): Borrow first row.
// - end(): Borrow end position.
// - operator[](): Borrow unchecked row.
// - get(): Borrow typed definition.
// - forEach(): Visit typed definitions.
// - visit(): Select checked position.
// - read(): Read native value.
// - write(): Apply native value.
// - readAs(): Read owning conversion.
// - writeAs(): Apply checked conversion.
template<class... Definitions>
class FieldTable {
	static_assert(sizeof...(Definitions) <= idComponentCapacity,
	              "Field table exceeds the 16-bit local position space");

public:
	using RootTypes = detail::TypeList<typename Definitions::Value...>;
	static constexpr std::size_t staticSize = sizeof...(Definitions);

	constexpr explicit FieldTable(Definitions... definitions) noexcept
	    : definitions_(definitions...),
	      entries_(makeEntries(std::index_sequence_for<Definitions...>{}))
	{}

	// Runtime-function/custom bindings can borrow definitions_ in this exact
	// table. Direct owner/slot contexts do not make all tables relocatable.
	FieldTable(const FieldTable&) = delete;
	FieldTable& operator=(const FieldTable&) = delete;
	FieldTable(FieldTable&&) = delete;
	FieldTable& operator=(FieldTable&&) = delete;

	[[nodiscard]] constexpr const FieldEntry* data() const& noexcept
	{
		return entries_.data();
	}

	const FieldEntry* data() const&& = delete;

	[[nodiscard]] constexpr std::size_t size() const noexcept
	{
		return staticSize;
	}

	// Erased iteration borrows this table; typed traversal below borrows its
	// exact definitions. Neither interface stores or materializes a value.
	[[nodiscard]] constexpr bool empty() const noexcept
	{
		return staticSize == 0;
	}

	[[nodiscard]] constexpr const FieldEntry* begin() const& noexcept
	{
		return entries_.data();
	}

	const FieldEntry* begin() const&& = delete;

	[[nodiscard]] constexpr const FieldEntry* end() const& noexcept
	{
		// std::array may expose checked iterators on other standard libraries.
		// Our view uses pointers and never adds zero to a possibly null data().
		if constexpr (staticSize == 0)
			return entries_.data();
		else
			return entries_.data() + staticSize;
	}

	const FieldEntry* end() const&& = delete;

	// Unchecked row access: i must be smaller than size(). Use visit() or an
	// index lookup when selecting a position from fallible external input.
	[[nodiscard]] constexpr const FieldEntry& operator[](std::size_t i) const& noexcept
	{
		return entries_[i];
	}

	const FieldEntry& operator[](std::size_t) const&& = delete;

	template<auto Position>
	[[nodiscard]] constexpr decltype(auto) get() const& noexcept
	{
		constexpr auto i = telemetry::detail::positionValue<Position>();
		static_assert(i < staticSize, "Field position is outside this table");
		if constexpr (i < staticSize)
			return std::get<static_cast<std::size_t>(i)>(definitions_);
	}

	template<auto Position>
	void get() const&& = delete;

	template<class Visitor>
	constexpr void forEach(Visitor&& visitor) const&
	{
		detail::forEachDefinition(definitions_, visitor, std::index_sequence_for<Definitions...>{});
	}

	template<class Visitor>
	void forEach(Visitor&&) const&& = delete;

	// A local position may be an integer or scoped position enum. Invalid
	// positions return false without invoking the visitor. Callback returns
	// are ignored; the bool reports selection, not the endpoint's status.
	template<class... Explicit, class Position, class Visitor>
	    requires(sizeof...(Explicit) == 0 && telemetry::detail::isIdInput<Position>)
	[[nodiscard]] bool visit(Position position, Visitor&& visitor) const&
	{
		if (!telemetry::detail::indexFits<std::uint32_t>(position))
			return false;
		const auto i = static_cast<std::uint32_t>(position);
		if (i >= staticSize)
			return false;
		using Dispatch = detail::DefinitionDispatch<std::tuple<Definitions...>,
		                                            std::remove_reference_t<Visitor>>;
		Dispatch::entries[i](definitions_, visitor);
		return true;
	}

	template<class... Explicit, class Position, class Visitor>
	    requires(sizeof...(Explicit) == 0 && telemetry::detail::isIdInput<Position>)
	bool visit(Position, Visitor&&) const&& = delete;

	template<auto Position>
	[[nodiscard]] auto read() const noexcept
	{
		constexpr auto i = telemetry::detail::positionValue<Position>();
		static_assert(i < staticSize, "Field position is outside this table");
		if constexpr (i < staticSize)
			return std::get<static_cast<std::size_t>(i)>(definitions_).read();
	}

	template<auto Position, class Argument>
	[[nodiscard]] telemetry::WriteResult write(Argument&& value) const noexcept
	{
		constexpr auto i = telemetry::detail::positionValue<Position>();
		static_assert(i < staticSize, "Field position is outside this table");
		if constexpr (i < staticSize)
			return std::get<static_cast<std::size_t>(i)>(definitions_)
			    .write(std::forward<Argument>(value));
	}

	// Static forms retain the concrete target. Runtime forms specialize an
	// indexed dispatch table for To/From and never construct legacy Scalar.
	template<class To, auto Position>
	    requires std::is_same_v<To, std::remove_cvref_t<To>>
	[[nodiscard]] std::optional<To> readAs() const& noexcept
	{
		return get<Position>().template readAs<To>();
	}

	template<class To, auto Position>
	void readAs() const&& = delete;

	template<auto Position, class From>
	    requires(!std::is_volatile_v<From>)
	[[nodiscard]] telemetry::WriteResult writeAs(const From& value) const& noexcept
	{
		return get<Position>().writeAs(value);
	}

	template<auto Position, class From>
	void writeAs(const From&) const&& = delete;

	template<class To, class... Explicit, class Position>
	    requires(sizeof...(Explicit) == 0 && telemetry::detail::isIdInput<Position> &&
	             std::is_same_v<To, std::remove_cvref_t<To>>)
	[[nodiscard]] std::optional<To> readAs(Position position) const& noexcept
	{
		static_assert(Type<To>::kind != TypeKind::Void,
		              "Field readAs requires a native value type");
		if (!telemetry::detail::indexFits<std::uint32_t>(position))
			return std::nullopt;
		const auto i = static_cast<std::uint32_t>(position);
		if (i >= staticSize)
			return std::nullopt;
		return detail::FieldAccessDispatch<std::tuple<Definitions...>, To>::reads[i](definitions_);
	}

	template<class To, class... Explicit, class Position>
	    requires(sizeof...(Explicit) == 0 && telemetry::detail::isIdInput<Position>)
	void readAs(Position) const&& = delete;

	template<class... Explicit, class Position, class From>
	    requires(sizeof...(Explicit) == 0 && telemetry::detail::isIdInput<Position> &&
	             !std::is_volatile_v<From>)
	[[nodiscard]] telemetry::WriteResult writeAs(Position position,
	                                             const From& value) const& noexcept
	{
		static_assert(Type<From>::kind != TypeKind::Void,
		              "Field writeAs requires a native value type");
		if (!telemetry::detail::indexFits<std::uint32_t>(position))
			return telemetry::WriteResult::NotFound;
		const auto i = static_cast<std::uint32_t>(position);
		if (i >= staticSize)
			return telemetry::WriteResult::NotFound;
		return detail::FieldAccessDispatch<std::tuple<Definitions...>, From>::writes[i](
		    definitions_, value);
	}

	template<class... Explicit, class Position, class From>
	    requires(sizeof...(Explicit) == 0 && telemetry::detail::isIdInput<Position>)
	void writeAs(Position, const From&) const&& = delete;

	// One immutable TypeId per declaration, assigned only after the complete
	// Model registry is known. This metadata has no binding/owner addresses.
	template<class Registry>
	struct TypeStorage {
		inline static constexpr std::array<TypeId, staticSize> entries{
		    {Registry::template typeId<typename Definitions::Value>()...}};
	};

private:
	template<class Definition>
	static constexpr std::uint32_t requiredScratch = [] {
		if constexpr (detail::localObject<typename Definition::Value>)
			return std::uint32_t{0};
		static_assert(scratchBytes<typename Definition::Value> <= UINT32_MAX,
		              "Field scratch size overflows u32");
		return static_cast<std::uint32_t>(scratchBytes<typename Definition::Value>);
	}();

	template<class Definition>
	static constexpr std::uint32_t requiredReadScratch =
	    Definition::borrowsValue ? 0 : requiredScratch<Definition>;

	template<class Definition>
	static constexpr std::uint32_t requiredWriteScratch =
	    Definition::writable ? requiredScratch<Definition> : 0;

	template<class Definition>
	static EncodedReadResult readOne(const void* raw, std::byte* bytes,
	                                 Workspace& workspace) noexcept
	{
		using Value = typename Definition::Value;
		using Getter = typename Definition::GetterBinding;
		const std::span<std::byte> output{bytes, wireSize<Value>};
		if constexpr (Definition::borrowsValue) {
			auto selected = detail::ErasedBinding<Getter>::snapshot(raw);
			if (!Getter::available(selected))
				return {DispatchStatus::Unavailable, 0};
			const Value& value = Getter::invoke(selected);
			if (!detail::encodeBorrowedEndpoint(value, output))
				return {DispatchStatus::InvalidPayload, 0};
			return {DispatchStatus::Ok, wireSize<Value>};
		} else if constexpr (detail::localObject<Value>) {
			auto selected = detail::ErasedBinding<Getter>::snapshot(raw);
			if (!Getter::available(selected))
				return {DispatchStatus::Unavailable, 0};
			const Value value = Getter::invoke(selected);
			detail::encodeEndpoint(value, output.first(wireSize<Value>));
			return {DispatchStatus::Ok, wireSize<Value>};
		} else {
			// The reservation itself checks remaining capacity and alignment.
			// The advertised scratchBytes includes worst-case alignment margin.
			auto lease = workspace.reserve<Value>();
			if (!lease.valid())
				return {DispatchStatus::WorkspaceTooSmall, 0};
			auto selected = detail::ErasedBinding<Getter>::snapshot(raw);
			if (!Getter::available(selected))
				return {DispatchStatus::Unavailable, 0};
			auto* value = lease.constructFrom([&]() -> Value {
				return Getter::invoke(selected);
			});
			detail::encodeEndpoint(*value, output.first(wireSize<Value>));
			return {DispatchStatus::Ok, wireSize<Value>};
		}
	}

	// Called only with a fully decoded exact native Value. Availability and
	// status validation surround the callback, with no partial owner mutation
	// caused by codec failure after the setter has started.
	template<class Definition>
	static EncodedWriteResult writeReady(const void* context,
	                                     const typename Definition::Value& value) noexcept
	{
		using Setter = typename Definition::SetterBinding;
		auto selected = detail::ErasedBinding<Setter>::snapshot(context);
		if (!Setter::available(selected))
			return {DispatchStatus::Unavailable};
		const auto result = Setter::invoke(selected, value);
		if (!model_detail::validStatus(result))
			return {DispatchStatus::InternalError};
		return {DispatchStatus::Ok, result};
	}

	template<class Definition>
	static EncodedWriteResult writeOne(const void* raw, const std::byte* bytes,
	                                   Workspace& workspace) noexcept
	{
		using Value = typename Definition::Value;
		const std::span<const std::byte> input{bytes, wireSize<Value>};
		if constexpr (detail::localObject<Value>) {
			if (!detail::validEndpoint<Value>(input))
				return {DispatchStatus::InvalidPayload};
			const Value value = detail::decodeLocalEndpoint<Value>(input);
			return writeReady<Definition>(raw, value);
		} else {
			auto lease = workspace.reserve<Value>();
			if (!lease.valid())
				return {DispatchStatus::WorkspaceTooSmall};
			Value* value = detail::decodeEndpoint<Value>(input, lease);
			if (value == nullptr)
				return {DispatchStatus::InvalidPayload};
			return writeReady<Definition>(raw, *value);
		}
	}

	template<class Definition>
	static consteval FieldEntry::Write writer() noexcept
	{
		if constexpr (Definition::writable)
			return &writeOne<Definition>;
		else
			return nullptr;
	}

	template<class Definition>
	static constexpr const void* writeContext(const Definition& definition) noexcept
	{
		if constexpr (Definition::writable)
			return detail::ErasedBinding<typename Definition::SetterBinding>::context(
			    definition.setter_);
		else
			return nullptr;
	}

	template<std::size_t... I>
	constexpr auto makeEntries(std::index_sequence<I...>) noexcept
	{
		return std::array<FieldEntry, staticSize>{{FieldEntry{
		    detail::ErasedBinding<typename Definitions::GetterBinding>::context(
		        std::get<I>(definitions_).getter_),
		    &readOne<Definitions>, writeContext(std::get<I>(definitions_)), writer<Definitions>(),
		    std::get<I>(definitions_).name(), wireSize<typename Definitions::Value>,
		    requiredReadScratch<Definitions>, requiredWriteScratch<Definitions>}...}};
	}

	std::tuple<Definitions...> definitions_;
	std::array<FieldEntry, staticSize> entries_;
};

template<class... Definitions>
FieldTable(Definitions...) -> FieldTable<Definitions...>;

} // namespace telemetry

#endif
