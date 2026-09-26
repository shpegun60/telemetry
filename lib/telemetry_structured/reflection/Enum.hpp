/*
 * @file Enum.hpp
 * @brief Compile-time enum dictionaries for structured telemetry.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */

#ifndef TELEMETRY_STRUCTURED_ENUM_HPP
#define TELEMETRY_STRUCTURED_ENUM_HPP

#include "detail/MagicEnumAdapter.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

namespace telemetry::structured::reflection {

// Specialize this before first use of Enum<E>. An explicit empty dictionary
// replaces automatic discovery just like a nonempty specialization.
template <class E>
struct EnumReflection {};

namespace detail {

template <class E>
inline constexpr bool scopedEnum = [] {
    if constexpr (std::is_enum_v<E>)
        return !std::is_convertible_v<E, std::underlying_type_t<E>>;
    else
        return false;
}();

template <class Raw>
inline constexpr bool integerWire =
    std::is_integral_v<Raw> && !std::is_same_v<Raw, bool> &&
    !std::is_same_v<Raw, char> && !std::is_same_v<Raw, wchar_t> &&
    !std::is_same_v<Raw, char8_t> && !std::is_same_v<Raw, char16_t> &&
    !std::is_same_v<Raw, char32_t> &&
    (sizeof(Raw) == 1 || sizeof(Raw) == 2 ||
     sizeof(Raw) == 4 || sizeof(Raw) == 8);

// Reject overlong encodings, surrogates, out-of-range code points and NUL.
// Explicit names are copied into their constexpr definitions before use.
constexpr bool validUtf8(std::string_view name) noexcept
{
    if (name.empty() || name.size() > 4096) return false;
    for (std::size_t i = 0; i < name.size();) {
        const auto first = static_cast<unsigned char>(name[i]);
        if (first == 0) return false;
        if (first < 0x80) {
            ++i;
            continue;
        }
        std::size_t extra = 0;
        unsigned char minSecond = 0x80;
        unsigned char maxSecond = 0xbf;
        if (first >= 0xc2 && first <= 0xdf) extra = 1;
        else if (first >= 0xe0 && first <= 0xef) {
            extra = 2;
            if (first == 0xe0) minSecond = 0xa0;
            if (first == 0xed) maxSecond = 0x9f;
        } else if (first >= 0xf0 && first <= 0xf4) {
            extra = 3;
            if (first == 0xf0) minSecond = 0x90;
            if (first == 0xf4) maxSecond = 0x8f;
        } else {
            return false;
        }
        if (extra > name.size() - i - 1) return false;
        const auto second = static_cast<unsigned char>(name[i + 1]);
        if (second < minSecond || second > maxSecond) return false;
        for (std::size_t j = 2; j <= extra; ++j) {
            const auto byte = static_cast<unsigned char>(name[i + j]);
            if (byte < 0x80 || byte > 0xbf) return false;
        }
        i += extra + 1;
    }
    return true;
}

constexpr bool asciiName(std::string_view name) noexcept
{
    if (name.empty() || name.size() > 4096) return false;
    const auto initial = [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
    };
    if (!initial(static_cast<unsigned char>(name[0]))) return false;
    for (std::size_t i = 1; i < name.size(); ++i) {
        const auto c = static_cast<unsigned char>(name[i]);
        if (!initial(c) && (c < '0' || c > '9')) return false;
    }
    return true;
}

template <class E>
struct EntryView {
    E value;
    std::string_view name;
};

template <class E, std::size_t N>
consteval auto sorted(std::array<EntryView<E>, N> entries, bool automatic)
{
    for (std::size_t i = 0; i < N; ++i) {
        if (automatic ? !asciiName(entries[i].name) : !validUtf8(entries[i].name))
            std::abort(); // Invalid name fails this consteval call.
    }
    for (std::size_t i = 0; i < N; ++i) {
        for (std::size_t j = i + 1; j < N; ++j) {
            const auto left = static_cast<std::underlying_type_t<E>>(entries[i].value);
            const auto right = static_cast<std::underlying_type_t<E>>(entries[j].value);
            if (left == right) std::abort(); // One name per numeric code.
            if (right < left) std::swap(entries[i], entries[j]);
        }
    }
    return entries;
}

template <class E, std::size_t N>
struct NamedEntry {
    using EnumType = E;

    E value;
    std::array<char, N> bytes;

    constexpr std::string_view name() const noexcept
    {
        return {bytes.data(), N - 1};
    }
};

template <class T>
inline constexpr bool namedEntry = false;

template <class E, std::size_t N>
inline constexpr bool namedEntry<NamedEntry<E, N>> = true;

template <class E, class... Entries>
struct NamedSet {
    std::tuple<Entries...> items;
};

template <class E, auto... Values>
struct CodeSet {};

template <auto Value>
consteval std::string_view exactName()
{
    using E = decltype(Value);
    // Inspect the raw result first. For an unnamed code, the pinned backend's
    // public helper can otherwise fail inside its own implementation.
    constexpr auto raw = detail::rawEnumName<Value>();
    static_assert(raw.size_ > detail::enumNamePrefixLength<E>,
                  "enumCodes value has no backend name; use enumEntry(value, name)");
    if constexpr (raw.size_ > detail::enumNamePrefixLength<E>)
        return detail::backendEnumName<Value>();
    else
        return {};
}

template <class E, auto... Values>
consteval auto normalize(CodeSet<E, Values...>)
{
    return sorted<E>(
        std::array<EntryView<E>, sizeof...(Values)>{
            EntryView<E>{Values, exactName<Values>()}...},
        false);
}

template <class E, class... Entries, std::size_t... I>
consteval auto normalizeNamed(const NamedSet<E, Entries...>& source,
                              std::index_sequence<I...>)
{
    return sorted<E>(
        std::array<EntryView<E>, sizeof...(Entries)>{
            EntryView<E>{std::get<I>(source.items).value,
                         std::get<I>(source.items).name()}...},
        false);
}

template <class E, class... Entries>
consteval auto normalize(const NamedSet<E, Entries...>& source)
{
    return normalizeNamed(source, std::index_sequence_for<Entries...>{});
}

template <class E>
consteval auto automatic()
{
    constexpr auto source = detail::backendEnumEntries<E>();
    constexpr std::size_t count = source.size();
    std::array<EntryView<E>, count> result{};
    for (std::size_t i = 0; i < count; ++i)
        result[i] = EntryView<E>{source[i].first, source[i].second};
    return sorted<E>(result, true);
}

} // namespace detail

// Own the name bytes, including when the caller used a temporary/local array
// to build the compile-time specialization. No borrowed name can dangle.
template <class E, class Char, std::size_t N>
    requires (std::is_enum_v<E> && (std::is_same_v<Char, char> ||
                                    std::is_same_v<Char, char8_t>))
consteval auto enumEntry(E value, const Char (&name)[N])
{
    static_assert(N > 1 && N <= 4097,
                  "Enum name length is outside 1..4096 bytes");
    detail::NamedEntry<E, N> result{value, {}};
    for (std::size_t i = 0; i < N; ++i) {
        if constexpr (std::is_same_v<Char, char>)
            result.bytes[i] = name[i];
        else
            result.bytes[i] = std::bit_cast<char>(static_cast<std::uint8_t>(name[i]));
    }
    if (result.bytes[N - 1] != 0 || !detail::validUtf8(result.name()))
        std::abort(); // Invalid UTF-8 or embedded NUL fails constant evaluation.
    return result;
}

template <auto First, auto... Rest>
consteval auto enumCodes()
{
    using E = decltype(First);
    static_assert(std::is_enum_v<E>, "enumCodes requires enum values");
    static_assert((std::is_same_v<E, decltype(Rest)> && ...),
                  "All enumCodes values must have one exact enum type");
    static_assert(1 + sizeof...(Rest) <= 65536,
                  "Enum dictionary exceeds the model entry ceiling");
    if constexpr ((std::is_same_v<E, decltype(Rest)> && ...)) {
        constexpr std::array<E, 1 + sizeof...(Rest)> codes{First, Rest...};
        static_assert([&] {
            for (std::size_t i = 0; i < codes.size(); ++i)
                for (std::size_t j = i + 1; j < codes.size(); ++j)
                    if (codes[i] == codes[j]) return false;
            return true;
        }(), "Duplicate explicit enum code");
    }
    return detail::CodeSet<E, First, Rest...>{};
}

template <class E>
consteval auto enumEntries()
{
    static_assert(std::is_enum_v<E>, "enumEntries<E>() requires an enum type");
    return detail::NamedSet<E>{std::tuple<>{}};
}

template <class First, class... Rest>
    requires (detail::namedEntry<First> && (detail::namedEntry<Rest> && ...))
consteval auto enumEntries(First first, Rest... rest)
{
    using E = typename First::EnumType;
    static_assert((std::is_same_v<E, typename Rest::EnumType> && ...),
                  "All enumEntries values must have one exact enum type");
    static_assert(1 + sizeof...(Rest) <= 65536,
                  "Enum dictionary exceeds the model entry ceiling");

    auto result = detail::NamedSet<E, First, Rest...>{
        std::tuple<First, Rest...>{first, rest...}};
    // Validate duplicate numeric codes at the point of declaration.
    constexpr std::size_t count = 1 + sizeof...(Rest);
    std::array<E, count> codes{};
    [&]<std::size_t... I>(std::index_sequence<I...>) {
        ((codes[I] = std::get<I>(result.items).value), ...);
    }(std::make_index_sequence<count>{});
    for (std::size_t i = 0; i < count; ++i)
        for (std::size_t j = i + 1; j < count; ++j)
            if (codes[i] == codes[j]) std::abort(); // Duplicate numeric code.
    return result;
}

template <class E>
struct Enum {
    static_assert(std::is_enum_v<E>, "Enum<E> requires an enum type");
    using Underlying = std::underlying_type_t<E>;
    static_assert(detail::scopedEnum<E>, "Structured enums must be scoped");
    static_assert(detail::integerWire<Underlying>,
                  "Structured enum underlying type must be a supported signed or unsigned integer");
private:
    inline static constexpr auto dictionary_ = []() consteval {
        if constexpr (requires { EnumReflection<E>::entries; })
            return detail::normalize(EnumReflection<E>::entries);
        else
            return detail::automatic<E>();
    }();

public:
    static constexpr std::size_t entryCount = dictionary_.size();
    static_assert(entryCount <= 65536, "Enum dictionary exceeds the model entry ceiling");

    template <std::size_t I>
    static constexpr E entryValue() noexcept
    {
        static_assert(I < entryCount, "Enum entry index is out of range");
        return dictionary_[I].value;
    }
    template <std::size_t I>
    static constexpr std::string_view entryName() noexcept
    {
        static_assert(I < entryCount, "Enum entry index is out of range");
        return dictionary_[I].name;
    }
};

} // namespace telemetry::structured::reflection

#endif
