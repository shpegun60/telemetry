/*
 * @file Name.hpp
 * @brief Validate borrowed endpoint/catalog names without changing stored layout.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */

/*
 * Validates names once while retaining the existing borrowed-pointer layout.
 *
 * Factories accept this implicit wrapper and definitions store only its
 * pointer. Array arguments retain their extent during validation; pointer
 * arguments use C-string semantics. The backing name must remain alive and
 * immutable for every definition, catalog and metadata view that borrows it.
 */

#ifndef TELEMETRY_DETAIL_NAME_HPP
#define TELEMETRY_DETAIL_NAME_HPP
#pragma once

#include "../type/Traits.hpp"
#include <telemetry/detail/Target.hpp>
#include <cstdlib>
#include <string_view>
#include <type_traits>

namespace telemetry::detail {

[[noreturn]] inline void invalidEndpointName() noexcept
{
	std::abort();
}

// Factories accept this implicit argument wrapper but store only its pointer.
// Successful construction guarantees a non-null, nonempty, valid UTF-8 name;
// endpoint/group constructors need not repeat address comparisons.
// Arrays retain their extent long enough to reject embedded NUL; pointer
// arguments denote a conventional NUL-terminated string. Neither is owned.
// Public methods:
// - Name(): Validate borrowed name.
// - operator const char*(): Borrow validated name.
class Name {
public:
	// Mutable buffers use C-string semantics, but never scan past the array.
	template<std::size_t N>
	constexpr Name(char (&text)[N]) noexcept : data_(text)
	{
		std::size_t size = 0;
		while (size < N && size <= Limits::maxStringBytes && text[size] != '\0')
			++size;
		if (size == N || size > Limits::maxStringBytes ||
		    !reflection::detail::validUtf8({text, size}))
			invalidEndpointName();
	}

	template<std::size_t N>
	constexpr Name(const char (&text)[N]) noexcept : data_(text)
	{
		static_assert(N <= Limits::maxStringBytes + 1, "Endpoint name exceeds string ceiling");
		if (text[N - 1] != '\0' || !reflection::detail::validUtf8({text, N - 1}))
			invalidEndpointName();
	}

	template<class Pointer>
	    requires(std::is_same_v<std::remove_cvref_t<Pointer>, const char*> ||
	             std::is_same_v<std::remove_cvref_t<Pointer>, char*>)
	constexpr Name(Pointer&& text) noexcept : data_(text)
	{
		// GCC null-check modes may not fold an address comparison during
		// constant evaluation. The shared helper preserves runtime checks;
		// the following byte scan also requires a real constexpr object.
		if (!telemetry::detail::pointerPresent(data_))
			invalidEndpointName();
		std::size_t size = 0;
		while (size <= Limits::maxStringBytes && data_[size] != '\0')
			++size;
		if (size > Limits::maxStringBytes || !reflection::detail::validUtf8({data_, size}))
			invalidEndpointName();
	}

	Name(std::nullptr_t) = delete;

	[[nodiscard]] constexpr operator const char*() const noexcept
	{
		return data_;
	}

private:
	const char* data_;
};

} // namespace telemetry::detail
#endif
