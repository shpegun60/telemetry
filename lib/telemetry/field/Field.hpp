/*
 * @file Field.hpp
 * @brief Exact native values and optional setters for structured fields.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */

/*
 * Declares a named native value with an optional exact-type setter.
 *
 * A getter either returns an owning value or borrows a const lvalue; read()
 * preserves that distinction. readAs explicitly requests an owning value and
 * permits checked numeric conversion. Definitions borrow names and bindings,
 * and applications provide synchronization and validation of their state.
 */

#ifndef TELEMETRY_FIELD_FIELD_HPP
#define TELEMETRY_FIELD_FIELD_HPP
#pragma once

#include "../detail/Binding.hpp"
#include "../detail/FieldAccess.hpp"
#include "../detail/Name.hpp"
#include <telemetry/result/EndpointStatus.hpp>
#include "../result/BorrowedValue.hpp"
#include <cstdlib>
#include <optional>

namespace telemetry {

template<class... Definitions>
class FieldTable;

namespace field_detail {
// Empty marker for an absent declared Field setter capability.
struct NoSetter {};

// Validates a Field getter and preserves owning/borrowed result shape.
template<class Binding>
struct GetterShape
    : reflection::EndpointTraits<reflection::EndpointKind::Field, typename Binding::Signature> {
	using Base =
	    reflection::EndpointTraits<reflection::EndpointKind::Field, typename Binding::Signature>;
	using Facts = reflection::Function<typename Binding::Signature>;
	using RawResult = typename Facts::Result;
	using Value = std::remove_cvref_t<RawResult>;
	static_assert(Facts::arity == 0, "Field getter must take no arguments");
	static_assert(
	    !std::is_void_v<Value> && (std::is_same_v<RawResult, Value> || Base::borrowsResult),
	    "Field getter must return an unqualified native value or an exact const lvalue reference");
	static_assert(Type<Value>::kind != TypeKind::Void, "Field value cannot be void");
};

template<class Binding, class Value>
consteval bool checkSetter()
{
	if constexpr (!std::is_same_v<Binding, NoSetter>) {
		using Shape = reflection::EndpointTraits<reflection::EndpointKind::Field,
		                                         typename Binding::Signature>;
		static_assert(std::is_same_v<typename Shape::Result, telemetry::WriteResult>,
		              "Field setter must return telemetry::WriteResult");
		static_assert(Shape::Callable::arity == 1 && std::is_same_v<typename Shape::Request, Value>,
		              "Field setter must accept the exact getter type by value or const reference");
	}
	return true;
}
} // namespace field_detail

// A named typed getter plus a declared optional setter capability. read()
// returns optional<Value> for owning getters or BorrowedValue<Value> for a
// const-reference getter. The definition borrows name and binding state;
// synchronization and coherence across different Fields belong to the owner.
// Public methods:
// - FieldDefinition(): Bind named endpoints.
// - name(): Borrow endpoint name.
// - read(): Read native value.
// - write(): Apply native value.
// - readAs(): Read owning conversion.
// - writeAs(): Apply checked conversion.
template<class Getter, class Setter = field_detail::NoSetter>
class FieldDefinition {
public:
	using Value = typename field_detail::GetterShape<Getter>::Value;
	static constexpr bool borrowsValue = field_detail::GetterShape<Getter>::borrowsResult;
	using ReadResult = std::conditional_t<borrowsValue, BorrowedValue<Value>, std::optional<Value>>;
	static constexpr bool writable = !std::is_same_v<Setter, field_detail::NoSetter>;
	static_assert(field_detail::checkSetter<Setter, Value>());

	constexpr FieldDefinition(detail::Name name, Getter getter, Setter setter = {}) noexcept
	    : name_(name), getter_(getter), setter_(setter)
	{} // detail::Name already validated the borrowed metadata.

	[[nodiscard]] constexpr const char* name() const noexcept
	{
		return name_;
	}

	// Keep this tiny binding adapter visible at a known call site. Runtime
	// As dispatch can otherwise make -Os outline it for unrelated callers,
	// hiding the direct owner's address and adding an optional return roundtrip.
	[[nodiscard]] TELEMETRY_FORCE_INLINE ReadResult read() const noexcept
	{
		auto selected = getter_.snapshot();
		if (!Getter::available(selected))
			return {};
		if constexpr (borrowsValue)
			return ReadResult::from(Getter::invoke(selected));
		else
			return Getter::invoke(selected);
	}

	// Exact native writes preserve the argument's value/reference category.
	// An absent declared setter is ReadOnly; an empty setter slot is Unavailable.
	template<class Argument>
	    requires(std::is_same_v<std::remove_cvref_t<Argument>, Value> &&
	             !std::is_volatile_v<std::remove_reference_t<Argument>>)
	[[nodiscard]] telemetry::WriteResult write(Argument&& value) const noexcept
	{
		if constexpr (!writable) {
			return telemetry::WriteResult::ReadOnly;
		} else {
			auto selected = setter_.snapshot();
			if (!Setter::available(selected))
				return telemetry::WriteResult::Unavailable;
			return Setter::invoke(selected, std::forward<Argument>(value));
		}
	}

	// Request an owning result. Only exact structural types or checked numeric
	// conversions are supported, so this opt-in never hides a generic container.
	template<class To>
	    requires std::is_same_v<To, std::remove_cvref_t<To>>
	[[nodiscard]] TELEMETRY_FORCE_INLINE std::optional<To> readAs() const noexcept
	{
		static_assert(Type<To>::kind != TypeKind::Void,
		              "Field readAs requires a native value type");
		static_assert(std::is_same_v<To, Value> ||
		                  (detail::nativeNumber<To> && detail::nativeNumber<Value>),
		              "Field readAs permits numeric conversion or the exact structural type");
		return detail::readFieldAs<To>(*this);
	}

	// Complete conversion before calling the setter. Invalid numeric input
	// cannot reach application code or modify the Field through this interface.
	template<class From>
	    requires(!std::is_volatile_v<From>)
	[[nodiscard]] telemetry::WriteResult writeAs(const From& value) const noexcept
	{
		static_assert(Type<From>::kind != TypeKind::Void,
		              "Field writeAs requires a native value type");
		static_assert(std::is_same_v<From, Value> ||
		                  (detail::nativeNumber<From> && detail::nativeNumber<Value>),
		              "Field writeAs permits numeric conversion or the exact structural type");
		return detail::writeFieldAs(*this, value);
	}

private:
	template<class... Definitions>
	friend class FieldTable;
	using GetterBinding = Getter;
	using SetterBinding = Setter;
	const char* name_;
	Getter getter_;
	[[no_unique_address]] Setter setter_;
};

template<auto Get>
    requires detail::functionPointer<decltype(Get)>
[[nodiscard]] constexpr auto field(detail::Name name) noexcept
{
	return FieldDefinition{name, detail::StaticFunction<Get>{}};
}

template<auto Get, auto Set>
    requires(detail::functionPointer<decltype(Get)> && detail::functionPointer<decltype(Set)>)
[[nodiscard]] constexpr auto field(detail::Name name) noexcept
{
	return FieldDefinition{name, detail::StaticFunction<Get>{}, detail::StaticFunction<Set>{}};
}

// Explicit owner type arguments are deliberately not accepted: deduction
// preserves the actual object's category, including braced-temporary cases.
template<auto Get, class... Explicit, class Owner>
    requires(sizeof...(Explicit) == 0 && std::is_member_function_pointer_v<decltype(Get)> &&
             detail::StableOwner<Owner>)
[[nodiscard]] constexpr auto field(detail::Name name, Owner&& owner) noexcept
{
	return FieldDefinition{name, detail::method<Get>(std::forward<Owner>(owner))};
}

template<auto Get, auto Set, class... Explicit, class Owner>
    requires(sizeof...(Explicit) == 0 && std::is_member_function_pointer_v<decltype(Get)> &&
             std::is_member_function_pointer_v<decltype(Set)> && detail::StableOwner<Owner>)
[[nodiscard]] constexpr auto field(detail::Name name, Owner&& owner) noexcept
{
	return FieldDefinition{name, detail::method<Get>(std::forward<Owner>(owner)),
	                       detail::method<Set>(std::forward<Owner>(owner))};
}

template<class... Explicit, class Getter>
    requires(sizeof...(Explicit) == 0 && detail::Bindable<Getter>)
[[nodiscard]] constexpr auto field(detail::Name name, Getter&& getter) noexcept
{
	return FieldDefinition{name, detail::binding(std::forward<Getter>(getter))};
}

template<class... Explicit, class Getter, class Setter>
    requires(sizeof...(Explicit) == 0 && detail::Bindable<Getter> && detail::Bindable<Setter>)
[[nodiscard]] constexpr auto field(detail::Name name, Getter&& getter, Setter&& setter) noexcept
{
	return FieldDefinition{name, detail::binding(std::forward<Getter>(getter)),
	                       detail::binding(std::forward<Setter>(setter))};
}

// Explain legacy declaration mistakes at the factory boundary. These
// overloads store nothing and can never form a usable definition.
template<class Getter, class Setter>
    requires(!detail::callable<Getter> || !detail::callable<Setter>)
constexpr void field(const char*, Getter&&, Setter&&) noexcept
{
	static_assert(
	    detail::callable<Getter> && detail::callable<Setter>,
	    "Structured field accepts name and bindings only; semantic metadata is not supported");
}

} // namespace telemetry

#endif
