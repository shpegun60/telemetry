/*
 * @file EnumProbe.cpp
 * @brief Stage 03 enum normalization and explicit dictionary checks.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */

#include <telemetry/reflection/Enum.hpp>

#include <cstdint>
#include <string_view>
#include <type_traits>

enum class Mode : std::uint8_t { Off, Auto, Manual };
enum class DefaultMode { Off, Auto };
enum class Error : std::uint16_t { None = 0, High = 1000, Higher = 2000 };
enum class Alias : std::uint8_t { Ready = 1, Ok = 1 };
enum class Empty : std::int32_t { Far = 1000 };
enum class SignedError : std::int64_t { Negative = -1000, Positive = 2000 };
enum class CopiedName : std::uint8_t { Value };
enum class CustomError : std::uint16_t { Far = 30000, Farther = 50000 };

consteval auto makeCopiedName()
{
    char local[] = "copied";
    return telemetry::reflection::enumEntries(telemetry::reflection::enumEntry(CopiedName::Value, local));
}

template <> struct telemetry::reflection::EnumReflection<Error> {
    inline static constexpr auto entries = telemetry::reflection::enumCodes<
        Error::Higher, Error::None, Error::High>();
};

template <> struct telemetry::reflection::EnumReflection<Alias> {
    inline static constexpr auto entries = telemetry::reflection::enumEntries(
        telemetry::reflection::enumEntry(Alias::Ready, u8"Réady"));
};

template <> struct telemetry::reflection::EnumReflection<Empty> {
    inline static constexpr auto entries = telemetry::reflection::enumEntries<Empty>();
};

template <> struct telemetry::reflection::EnumReflection<SignedError> {
    inline static constexpr auto entries = telemetry::reflection::enumCodes<
        SignedError::Positive, SignedError::Negative>();
};

template <> struct telemetry::reflection::EnumReflection<CopiedName> {
    inline static constexpr auto entries = makeCopiedName();
};

template <> struct telemetry::reflection::EnumReflection<CustomError> {
    inline static constexpr auto entries = telemetry::reflection::enumEntries(
        telemetry::reflection::enumEntry(CustomError::Farther, "Second"),
        telemetry::reflection::enumEntry(CustomError::Far, "First"));
};

static_assert(telemetry::reflection::Enum<Mode>::entryCount == 3);
static_assert(telemetry::reflection::Enum<Mode>::entryName<1>() == "Auto");
static_assert(telemetry::reflection::Enum<DefaultMode>::entryCount == 2);
static_assert(std::is_same_v<telemetry::reflection::Enum<DefaultMode>::Underlying,
                             int>);
static_assert(std::is_same_v<telemetry::reflection::Enum<Mode>::Underlying, std::uint8_t>);
static_assert(telemetry::reflection::Enum<Error>::entryCount == 3);
static_assert(telemetry::reflection::Enum<Error>::entryValue<0>() == Error::None);
static_assert(telemetry::reflection::Enum<Error>::entryValue<1>() == Error::High);
static_assert(telemetry::reflection::Enum<Error>::entryValue<2>() == Error::Higher);
static_assert(telemetry::reflection::Enum<Error>::entryName<2>() == "Higher");
static_assert(telemetry::reflection::Enum<Alias>::entryCount == 1);
static_assert(telemetry::reflection::Enum<Alias>::entryName<0>() == "R\xC3\xA9" "ady");
static_assert(telemetry::reflection::Enum<Empty>::entryCount == 0);
static_assert(telemetry::reflection::Enum<SignedError>::entryValue<0>() == SignedError::Negative);
static_assert(telemetry::reflection::Enum<SignedError>::entryName<1>() == "Positive");
static_assert(telemetry::reflection::Enum<CopiedName>::entryName<0>() == "copied");
static_assert(telemetry::reflection::Enum<CustomError>::entryCount == 2);
static_assert(telemetry::reflection::Enum<CustomError>::entryValue<0>() ==
              CustomError::Far);
static_assert(telemetry::reflection::Enum<CustomError>::entryName<0>() ==
              "First");
static_assert(static_cast<std::uint8_t>(static_cast<Mode>(7)) == 7);

[[gnu::noinline]] std::string_view copiedName() noexcept
{
    return telemetry::reflection::Enum<CopiedName>::entryName<0>();
}

int main()
{
    const auto alias = telemetry::reflection::Enum<Alias>::entryName<0>();
    return alias == "R\xC3\xA9" "ady" && copiedName() == "copied" ? 0 : 1;
}
