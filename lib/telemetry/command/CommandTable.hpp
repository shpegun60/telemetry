/*
 * @file CommandTable.hpp
 * @brief Native command calls and checked encoded execution with caller scratch.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */

/*
 * Keeps heterogeneous Command definitions beside their erased runtime entries.
 *
 * Typed positional calls retain the original binding. Encoded execution checks
 * the complete request before invoking it and chooses local or Workspace
 * storage at compile time. Entries can point into this table's definitions,
 * which is why the table cannot move or copy.
 */

#ifndef TELEMETRY_COMMAND_COMMAND_TABLE_HPP
#define TELEMETRY_COMMAND_COMMAND_TABLE_HPP
#pragma once

#include "Command.hpp"
#include "../result/EndpointResults.hpp"
#include "../detail/Encoded.hpp"
#include "../detail/Traversal.hpp"
#include "../type/Registry.hpp"
#include <telemetry/core/Id.hpp>
#include <array>

namespace telemetry {

// Borrowed erased command row. executeEncoded establishes exact input length
// and scratch disjointness before its raw invoke member is used. Context and
// name storage remain owned by the application or the nonmoving local table.
// Public methods:
// - executeEncoded(): Execute encoded request.
struct CommandEntry {
	// The raw operation is internal; executeEncoded checks the byte boundary.
	using Invoke = EncodedCommandResult (*)(const void*, const std::byte*, Workspace&) noexcept;
	const void* context;
	Invoke invoke;
	const char* name;
	std::uint32_t requestWireBytes;
	std::uint32_t scratchBytes;

	[[nodiscard]] EncodedCommandResult executeEncoded(std::span<const std::byte> input,
	                                                  Workspace& workspace) const noexcept
	{
		if (input.size() != requestWireBytes)
			return {DispatchStatus::InvalidPayload};
		if (scratchBytes != 0 && buffersOverlap(input, workspace.storage()))
			return {DispatchStatus::InvalidPayload};
		return invoke(context, input.data(), workspace);
	}
};

// Owns declarations and generated runtime rows at a fixed address. Static
// call() retains exact request types and targets; traversal borrows definitions.
// No requests or completion state are kept in the table. Runtime contexts may
// point into definitions_, so copying or moving the table is unsupported.
// Public methods:
// - CommandTable(): Own Command definitions.
// - data(): Borrow runtime rows.
// - size(): Count declared Commands.
// - empty(): Check table emptiness.
// - begin(): Borrow first row.
// - end(): Borrow end position.
// - operator[](): Borrow unchecked row.
// - get(): Borrow typed definition.
// - forEach(): Visit typed definitions.
// - visit(): Select checked position.
// - call(): Invoke native command.
template<class... Definitions>
class CommandTable {
	static_assert(sizeof...(Definitions) <= idComponentCapacity,
	              "Command table exceeds the 16-bit local position space");

public:
	// One positional request root per command, including Void for no request.
	using RootTypes = detail::TypeList<typename Definitions::Request...>;
	using RegistryRootTypes = typename detail::UniqueFirst<RootTypes>::type;
	static constexpr std::size_t staticSize = sizeof...(Definitions);

	constexpr explicit CommandTable(Definitions... definitions) noexcept
	    : definitions_(definitions...),
	      entries_(makeEntries(std::index_sequence_for<Definitions...>{}))
	{}

	CommandTable(const CommandTable&) = delete;
	CommandTable& operator=(const CommandTable&) = delete;
	CommandTable(CommandTable&&) = delete;
	CommandTable& operator=(CommandTable&&) = delete;

	[[nodiscard]] constexpr const CommandEntry* data() const& noexcept
	{
		return entries_.data();
	}

	const CommandEntry* data() const&& = delete;

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

	[[nodiscard]] constexpr const CommandEntry* begin() const& noexcept
	{
		return entries_.data();
	}

	const CommandEntry* begin() const&& = delete;

	[[nodiscard]] constexpr const CommandEntry* end() const& noexcept
	{
		// std::array may expose checked iterators on other standard libraries.
		// Our view uses pointers and never adds zero to a possibly null data().
		if constexpr (staticSize == 0)
			return entries_.data();
		else
			return entries_.data() + staticSize;
	}

	const CommandEntry* end() const&& = delete;

	// Unchecked row access requires i < size(); visit() checks external local
	// positions without narrowing them before validation.
	[[nodiscard]] constexpr const CommandEntry& operator[](std::size_t i) const& noexcept
	{
		return entries_[i];
	}

	const CommandEntry& operator[](std::size_t) const&& = delete;

	template<auto Position>
	[[nodiscard]] constexpr decltype(auto) get() const& noexcept
	{
		constexpr auto i = telemetry::detail::positionValue<Position>();
		static_assert(i < staticSize, "Command position is outside this table");
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

	template<auto Position, class... Args>
	[[nodiscard]] telemetry::CommandResult call(Args&&... args) const noexcept
	{
		constexpr auto i = telemetry::detail::positionValue<Position>();
		static_assert(i < staticSize, "Command position is outside this table");
		if constexpr (i < staticSize)
			return std::get<static_cast<std::size_t>(i)>(definitions_)
			    .call(std::forward<Args>(args)...);
	}

	// Request TypeIds are metadata against the final Model registry, not
	// locally assigned numbers or addresses of live request objects.
	template<class Registry>
	struct TypeStorage {
		inline static constexpr std::array<TypeId, staticSize> entries{
		    {Registry::template typeId<typename Definitions::Request>()...}};
	};

private:
	template<class Definition>
	static constexpr std::uint32_t requiredScratch = [] {
		if constexpr (std::is_void_v<typename Definition::Request>)
			return std::uint32_t{0};
		else if constexpr (detail::localObject<typename Definition::Request>)
			return std::uint32_t{0};
		else {
			static_assert(scratchBytes<typename Definition::Request> <= UINT32_MAX,
			              "Command scratch size overflows u32");
			return static_cast<std::uint32_t>(scratchBytes<typename Definition::Request>);
		}
	}();

	template<class Definition, class... Args>
	static EncodedCommandResult invokeReady(const void* context, Args&&... args) noexcept
	{
		using Binding = typename Definition::BindingType;
		auto selected = detail::ErasedBinding<Binding>::snapshot(context);
		if (!Binding::available(selected))
			return {DispatchStatus::Unavailable};
		const auto result = Binding::invoke(selected, std::forward<Args>(args)...);
		if (!model_detail::validStatus(result))
			return {DispatchStatus::InternalError};
		return {DispatchStatus::Ok, result};
	}

	// Complete representation validation and request construction before target
	// selection. A rejected payload therefore invokes no application callback.
	template<class Definition>
	static EncodedCommandResult invokeOne(const void* raw, const std::byte* bytes,
	                                      Workspace& workspace) noexcept
	{
		using Request = typename Definition::Request;
		const std::span<const std::byte> input{bytes, wireSize<Request>};
		if constexpr (std::is_void_v<Request>) {
			return invokeReady<Definition>(raw);
		} else if constexpr (detail::localObject<Request>) {
			if (!detail::validEndpoint<Request>(input))
				return {DispatchStatus::InvalidPayload};
			const Request request = detail::decodeLocalEndpoint<Request>(input);
			return invokeReady<Definition>(raw, request);
		} else {
			auto lease = workspace.reserve<Request>();
			if (!lease.valid())
				return {DispatchStatus::WorkspaceTooSmall};
			Request* request = detail::decodeEndpoint<Request>(input, lease);
			if (request == nullptr)
				return {DispatchStatus::InvalidPayload};
			return invokeReady<Definition>(raw, *request);
		}
	}

	template<std::size_t... I>
	constexpr auto makeEntries(std::index_sequence<I...>) noexcept
	{
		return std::array<CommandEntry, staticSize>{{CommandEntry{
		    detail::ErasedBinding<typename Definitions::BindingType>::context(
		        std::get<I>(definitions_).binding_),
		    &invokeOne<Definitions>, std::get<I>(definitions_).name(),
		    wireSize<typename Definitions::Request>, requiredScratch<Definitions>}...}};
	}

	std::tuple<Definitions...> definitions_;
	std::array<CommandEntry, staticSize> entries_;
};

template<class... Definitions>
CommandTable(Definitions...) -> CommandTable<Definitions...>;

} // namespace telemetry

#endif
