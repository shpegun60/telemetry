/*
 * @file Command.hpp
 * @brief Native commands with zero or one exact request structure.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */

/*
 * Declares a named native operation with no request or one aggregate request.
 *
 * The binding type preserves known targets and exact request types for direct
 * calls. Names, method owners, callable lvalues and slots are borrowed. Each
 * call resolves a late-bound target once and reports absence without running
 * application code; request validation and synchronization remain external.
 */

#ifndef TELEMETRY_COMMAND_COMMAND_HPP
#define TELEMETRY_COMMAND_COMMAND_HPP
#pragma once

#include "../detail/Binding.hpp"
#include "../detail/Name.hpp"
#include <telemetry/result/EndpointStatus.hpp>
#include <cstdlib>

namespace telemetry {

template<class... Definitions>
class CommandTable;

// A typed declaration, not a queued operation or transport request. The
// definition borrows its name and any owner/callable/slot represented by its
// binding. call() accepts only the declared request shape and preserves the
// callback's native status; an empty late-bound target returns Unavailable.
// Public methods:
// - CommandDefinition(): Bind named operation.
// - name(): Borrow endpoint name.
// - call(): Invoke native command.
template<class Binding>
class CommandDefinition {
	using Shape =
	    reflection::EndpointTraits<reflection::EndpointKind::Command, typename Binding::Signature>;

public:
	using Request = typename Shape::Request;
	static_assert(std::is_same_v<typename Shape::Result, telemetry::CommandResult>,
	              "Command must return telemetry::CommandResult");
	static_assert(std::is_void_v<Request> || Type<Request>::kind == TypeKind::Struct,
	              "Command request must be an aggregate struct or void");

	constexpr CommandDefinition(detail::Name name, Binding binding) noexcept
	    : name_(name), binding_(binding)
	{} // detail::Name already validated the borrowed metadata.

	[[nodiscard]] constexpr const char* name() const noexcept
	{
		return name_;
	}

	[[nodiscard]] telemetry::CommandResult call() const noexcept
	    requires std::is_void_v<Request>
	{
		return callResolved();
	}

	template<class Argument>
	    requires(!std::is_void_v<Request> &&
	             std::is_same_v<std::remove_cvref_t<Argument>, Request> &&
	             !std::is_volatile_v<std::remove_reference_t<Argument>>)
	[[nodiscard]] telemetry::CommandResult call(Argument&& request) const noexcept
	{
		return callResolved(std::forward<Argument>(request));
	}

private:
	template<class... Definitions>
	friend class CommandTable;
	using BindingType = Binding;

	// Check and invoke the same selected target. Rebinding or destroying the
	// referenced owner/slot during this interval is an application violation.
	template<class... Args>
	[[nodiscard]] telemetry::CommandResult callResolved(Args&&... args) const noexcept
	{
		auto selected = binding_.snapshot();
		if (!Binding::available(selected))
			return telemetry::CommandResult::Unavailable;
		return Binding::invoke(selected, std::forward<Args>(args)...);
	}

	const char* name_;
	Binding binding_;
};

template<auto Target>
    requires detail::functionPointer<decltype(Target)>
[[nodiscard]] constexpr auto command(detail::Name name) noexcept
{
	return CommandDefinition{name, detail::StaticFunction<Target>{}};
}

template<auto Target, class... Explicit, class Owner>
    requires(sizeof...(Explicit) == 0 && std::is_member_function_pointer_v<decltype(Target)> &&
             detail::StableOwner<Owner>)
[[nodiscard]] constexpr auto command(detail::Name name, Owner&& owner) noexcept
{
	return CommandDefinition{name, detail::method<Target>(std::forward<Owner>(owner))};
}

template<class... Explicit, class Callable>
    requires(sizeof...(Explicit) == 0 && detail::Bindable<Callable>)
[[nodiscard]] constexpr auto command(detail::Name name, Callable&& callable) noexcept
{
	return CommandDefinition{name, detail::binding(std::forward<Callable>(callable))};
}

template<class First, class Second, class... Rest>
constexpr void command(const char*, First&&, Second&&, Rest&&...) noexcept
{
	static_assert(
	    !std::is_same_v<First, First>,
	    "Structured command accepts name and one binding only; semantic metadata is not supported");
}

} // namespace telemetry

#endif
