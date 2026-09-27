/*
 * @file Name.hpp
 * @brief Validate borrowed endpoint/catalog names without changing stored layout.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#ifndef TELEMETRY_STRUCTURED_DETAIL_NAME_HPP
#define TELEMETRY_STRUCTURED_DETAIL_NAME_HPP

#include "../type/Traits.hpp"
#include <cstdlib>
#include <string_view>
#include <type_traits>

namespace telemetry::structured::detail {

[[noreturn]] inline void invalidEndpointName() noexcept { std::abort(); }

// Factories accept this implicit argument wrapper but store only its pointer.
// Arrays retain their extent long enough to reject embedded NUL; pointer
// arguments denote a conventional NUL-terminated string. Neither is owned.
class Name {
public:
    // Mutable buffers use C-string semantics, but never scan past the array.
    template <std::size_t N>
    constexpr Name(char (&text)[N]) noexcept : data_(text)
    {
        std::size_t size = 0;
        while (size < N && size <= Limits::maxStringBytes && text[size] != '\0') ++size;
        if (size == N || size > Limits::maxStringBytes ||
            !reflection::detail::validUtf8({text, size})) invalidEndpointName();
    }

    template <std::size_t N>
    constexpr Name(const char (&text)[N]) noexcept : data_(text)
    {
        static_assert(N <= Limits::maxStringBytes + 1, "Endpoint name exceeds string ceiling");
        if (text[N - 1] != '\0' || !reflection::detail::validUtf8({text, N - 1}))
            invalidEndpointName();
    }

    template <class Pointer>
        requires (std::is_same_v<std::remove_cvref_t<Pointer>, const char*> ||
                  std::is_same_v<std::remove_cvref_t<Pointer>, char*>)
    constexpr Name(Pointer&& text) noexcept : data_(text)
    {
        if (data_ == nullptr) invalidEndpointName();
        std::size_t size = 0;
        while (size <= Limits::maxStringBytes && data_[size] != '\0') ++size;
        if (size > Limits::maxStringBytes || !reflection::detail::validUtf8({data_, size}))
            invalidEndpointName();
    }

    Name(std::nullptr_t) = delete;
    [[nodiscard]] constexpr operator const char*() const noexcept { return data_; }

private:
    const char* data_;
};

} // namespace telemetry::structured::detail
#endif
