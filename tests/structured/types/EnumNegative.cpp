/*
 * @file EnumNegative.cpp
 * @brief Compile-time rejection cases for enum dictionaries.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */

#include <telemetry_structured/reflection/Enum.hpp>

#include <cstdint>
#include <string>
#include <string_view>

enum class A : std::uint8_t { Zero, One, Far = 200 };
enum class B : std::uint8_t { Zero };
enum Unscoped { Item };

#if CASE == 1
inline constexpr auto bad = telemetry::structured::reflection::enumCodes<A::Zero, A::Zero>();
template <> struct telemetry::structured::reflection::EnumReflection<A> { inline static constexpr auto entries = bad; };
static_assert(telemetry::structured::reflection::Enum<A>::entryCount == 2);
#elif CASE == 2
inline constexpr auto bad = telemetry::structured::reflection::enumCodes<A::Zero, B::Zero>();
#elif CASE == 3
template <> struct telemetry::structured::reflection::EnumReflection<A> {
    inline static constexpr auto entries = telemetry::structured::reflection::enumCodes<static_cast<A>(1000)>();
};
static_assert(telemetry::structured::reflection::Enum<A>::entryCount == 1);
#elif CASE == 4
inline constexpr auto bad = telemetry::structured::reflection::enumEntry(A::Zero, "");
#elif CASE == 5
inline constexpr auto bad = telemetry::structured::reflection::enumEntry(A::Zero, "\xC0\xAF");
#elif CASE == 6
inline constexpr auto bad = telemetry::structured::reflection::enumEntry(A::Zero, "a\0b");
#elif CASE == 7
template <> struct telemetry::structured::reflection::EnumReflection<A> {
    inline static constexpr auto entries = telemetry::structured::reflection::enumEntries(
        telemetry::structured::reflection::enumEntry(A::Zero, "first"),
        telemetry::structured::reflection::enumEntry(A::Zero, "second"));
};
#elif CASE == 8
inline constexpr auto bad = telemetry::structured::reflection::enumEntry(A::Zero, std::string("local"));
#elif CASE == 9
static_assert(telemetry::structured::reflection::Enum<Unscoped>::entryCount == 1);
#elif CASE == 10
enum class Invalid : bool { No, Yes };
static_assert(telemetry::structured::reflection::Enum<Invalid>::entryCount == 2);
#elif CASE == 11
inline constexpr auto bad = telemetry::structured::reflection::enumEntry(A::Zero,
                                                                          "\xE0\x80\x80");
#elif CASE == 12
inline constexpr auto bad = telemetry::structured::reflection::enumEntry(A::Zero,
                                                                          "\xED\xA0\x80");
#elif CASE == 13
inline constexpr auto bad = telemetry::structured::reflection::enumEntry(A::Zero,
                                                                          "\xF4\x90\x80\x80");
#elif CASE == 14
inline constexpr auto bad = telemetry::structured::reflection::enumEntry(A::Zero,
                                                                          "\xE2\x82");
#elif CASE == 15
enum class NonAscii : std::uint8_t { ΔValue };
static_assert(telemetry::structured::reflection::Enum<NonAscii>::entryCount == 1);
#elif CASE == 16
inline constexpr auto bad = telemetry::structured::reflection::enumEntries(
    telemetry::structured::reflection::enumEntry(A::Zero, "Zero"),
    telemetry::structured::reflection::enumEntry(B::Zero, "Zero"));
#elif CASE == 17
enum class CharacterEnum : char { One = 1 };
static_assert(telemetry::structured::reflection::Enum<CharacterEnum>::entryCount == 1);
#elif CASE == 18
enum UnscopedFixed : std::uint8_t { FixedOne = 1 };
static_assert(telemetry::structured::reflection::Enum<UnscopedFixed>::entryCount == 1);
#elif CASE == 19
struct BorrowedEntry {
    using EnumType = A;
    A value = A::Zero;
    std::string_view name() const noexcept { return "borrowed"; }
};
inline constexpr auto bad = telemetry::structured::reflection::enumEntries(BorrowedEntry{});
#endif
