/*
 * @file Codec.hpp
 * @brief Canonical fixed-size little-endian codec for reflected C++ values.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */

#ifndef TELEMETRY_STRUCTURED_CODEC_CODEC_HPP
#define TELEMETRY_STRUCTURED_CODEC_CODEC_HPP

#include "Workspace.hpp"
#include "../type/Traits.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <span>
#include <type_traits>
#include <utility>

namespace telemetry::structured {

enum class CodecStatus : std::uint8_t {
    Ok,
    LengthMismatch,
    InvalidValue,
    WorkspaceTooSmall,
    Overlap,
    InvalidState
};

// Nontrivial aggregate construction explicitly names every member to bypass
// DMI. Bound only that template expansion; trivial large arrays use a loop.
inline constexpr std::uint32_t maxNeutralConstructionNodes = 1024;

// std::less supplies a total pointer order even for unrelated objects.
// Empty ranges have no bytes and therefore cannot overlap.
[[nodiscard]] TELEMETRY_FORCE_INLINE bool buffersOverlap(std::span<const std::byte> a,
                                         std::span<const std::byte> b) noexcept
{
    if (a.empty() || b.empty()) return false;
    const auto before = std::less<const std::byte*>{};
    return before(a.data(), b.data() + b.size()) &&
           before(b.data(), a.data() + a.size());
}

[[nodiscard]] inline bool buffersDisjoint(std::span<const std::byte> input,
                                          std::span<std::byte> output,
                                          std::span<std::byte> workspace) noexcept
{
    return !buffersOverlap(input, output) &&
           !buffersOverlap(input, workspace) &&
           !buffersOverlap(output, workspace);
}

namespace codec_detail {

struct Reader {
    std::span<const std::byte> bytes;
    std::size_t cursor = 0;

    template <class U>
    [[nodiscard]] U integer() noexcept
    {
        static_assert(std::is_unsigned_v<U> && !std::is_same_v<U, bool>);
        if constexpr (std::endian::native == std::endian::little) {
            // memcpy permits an unaligned byte source without creating a U&.
            // This is only a leaf copy: aggregate padding never reaches wire.
            U result;
            std::memcpy(&result, bytes.data() + cursor, sizeof(U));
            cursor += sizeof(U);
            return result;
        }
        using Shift = std::conditional_t<(sizeof(U) < sizeof(unsigned int)), unsigned int, U>;
        U result = 0;
        for (std::size_t i = 0; i < sizeof(U); ++i) {
            const auto part = static_cast<Shift>(
                std::to_integer<std::uint8_t>(bytes[cursor++])) << (8 * i);
            result = static_cast<U>(static_cast<Shift>(result) | part);
        }
        return result;
    }
};

struct Writer {
    std::span<std::byte> bytes;
    std::size_t cursor = 0;

    template <class U>
    void integer(U value) noexcept
    {
        static_assert(std::is_unsigned_v<U> && !std::is_same_v<U, bool>);
        if constexpr (std::endian::native == std::endian::little) {
            std::memcpy(bytes.data() + cursor, &value, sizeof(U));
            cursor += sizeof(U);
            return;
        }
        for (std::size_t i = 0; i < sizeof(U); ++i) {
            bytes[cursor++] = std::byte(static_cast<std::uint8_t>(value >> (8 * i)));
        }
    }
};

template <class T>
void encodeValue(const T& value, Writer& writer) noexcept;

template <class T>
[[nodiscard]] T decodeValue(Reader& reader) noexcept;

template <class T>
void decodeInto(T& value, Reader& reader) noexcept;

template <class T>
[[nodiscard]] T neutralValue() noexcept;

template <class T>
consteval std::uint32_t neutralConstructionNodes();

template <class T>
consteval bool hasBool();

template <class T, std::size_t... I>
void encodeMembers(const T& value, Writer& writer, std::index_sequence<I...>) noexcept
{
    (encodeValue(reflection::get<I>(value), writer), ...);
}

template <class T, std::size_t... I>
void decodeIntoMembers(T& value, Reader& reader, std::index_sequence<I...>) noexcept
{
    (decodeInto(reflection::get<I>(value), reader), ...);
}

template <class T, std::size_t... I>
[[nodiscard]] T neutralMembers(std::index_sequence<I...>) noexcept
{
    // Explicitly supply every member. A user-defined DMI never runs.
    return T{neutralValue<typename Type<T>::template Member<I>::Native>()...};
}

template <class T, std::size_t... I>
consteval std::uint32_t neutralMemberNodes(std::index_sequence<I...>)
{
    return (std::uint32_t{1} + ... +
            neutralConstructionNodes<typename Type<T>::template Member<I>::Native>());
}

template <class T>
consteval std::uint32_t neutralConstructionNodes()
{
    if constexpr (std::is_trivially_default_constructible_v<T> ||
                  Type<T>::kind == TypeKind::Scalar || Type<T>::kind == TypeKind::Enum) {
        return 1;
    } else if constexpr (Type<T>::kind == TypeKind::Array) {
        using Element = typename detail::ArrayInfo<T>::Element;
        return 1 + detail::ArrayInfo<T>::count * neutralConstructionNodes<Element>();
    } else {
        return neutralMemberNodes<T>(std::make_index_sequence<Type<T>::memberCount>{});
    }
}

template <class T, std::size_t... I>
[[nodiscard]] T neutralArray(std::index_sequence<I...>) noexcept
{
    using Element = typename detail::ArrayInfo<T>::Element;
    return T{{(static_cast<void>(I), neutralValue<Element>())...}};
}

template <class T>
[[nodiscard]] T neutralValue() noexcept
{
    if constexpr (neutralConstructionNodes<T>() > maxNeutralConstructionNodes) {
        static_assert(neutralConstructionNodes<T>() <= maxNeutralConstructionNodes,
                      "DMI construction expands too many members; split the DTO or remove DMI");
    } else if constexpr (Type<T>::kind == TypeKind::Scalar ||
                         Type<T>::kind == TypeKind::Enum ||
                         std::is_trivially_default_constructible_v<T>) {
        return T{};
    } else if constexpr (Type<T>::kind == TypeKind::Array) {
        return neutralArray<T>(std::make_index_sequence<detail::ArrayInfo<T>::count>{});
    } else {
        return neutralMembers<T>(std::make_index_sequence<Type<T>::memberCount>{});
    }
}

template <class T>
void encodeValue(const T& value, Writer& writer) noexcept
{
    if constexpr (Type<T>::kind == TypeKind::Scalar) {
        if constexpr (std::is_same_v<T, bool>) {
            writer.integer<std::uint8_t>(value ? 1 : 0);
        } else if constexpr (std::is_same_v<T, float>) {
            writer.integer(std::bit_cast<std::uint32_t>(value));
        } else if constexpr (std::is_same_v<T, double>) {
            writer.integer(std::bit_cast<std::uint64_t>(value));
        } else {
            using U = std::make_unsigned_t<T>;
            if constexpr (std::is_signed_v<T>)
                writer.integer(std::bit_cast<U>(value));
            else
                writer.integer(static_cast<U>(value));
        }
    } else if constexpr (Type<T>::kind == TypeKind::Enum) {
        using Raw = typename Type<T>::Underlying;
        encodeValue(static_cast<Raw>(value), writer);
    } else if constexpr (Type<T>::kind == TypeKind::Array) {
        for (const auto& element : value) encodeValue(element, writer);
    } else {
        encodeMembers(value, writer, std::make_index_sequence<Type<T>::memberCount>{});
    }
}

template <class T>
[[nodiscard]] T decodeValue(Reader& reader) noexcept
{
    if constexpr (Type<T>::kind == TypeKind::Scalar) {
        if constexpr (std::is_same_v<T, bool>) {
            return reader.integer<std::uint8_t>() != 0;
        } else if constexpr (std::is_same_v<T, float>) {
            return std::bit_cast<float>(reader.integer<std::uint32_t>());
        } else if constexpr (std::is_same_v<T, double>) {
            return std::bit_cast<double>(reader.integer<std::uint64_t>());
        } else {
            using U = std::make_unsigned_t<T>;
            const U bits = reader.integer<U>();
            if constexpr (std::is_signed_v<T>)
                return std::bit_cast<T>(bits);
            else
                return static_cast<T>(bits);
        }
    } else if constexpr (Type<T>::kind == TypeKind::Enum) {
        using Raw = typename Type<T>::Underlying;
        return static_cast<T>(decodeValue<Raw>(reader));
    } else {
        static_assert(Type<T>::kind == TypeKind::Scalar || Type<T>::kind == TypeKind::Enum,
                      "Aggregate values are decoded into caller-owned storage");
    }
}

template <class T>
void decodeInto(T& value, Reader& reader) noexcept
{
    if constexpr (Type<T>::kind == TypeKind::Scalar || Type<T>::kind == TypeKind::Enum) {
        value = decodeValue<T>(reader);
    } else if constexpr (Type<T>::kind == TypeKind::Array) {
        for (auto& element : value) decodeInto(element, reader);
    } else {
        decodeIntoMembers(value, reader,
                          std::make_index_sequence<Type<T>::memberCount>{});
    }
}

template <class T, std::size_t... I>
consteval bool membersHaveBool(std::index_sequence<I...>)
{
    return (false || ... || [] {
        using Member = typename Type<T>::template Member<I>::Native;
        return hasBool<Member>();
    }());
}

template <class T>
consteval bool hasBool()
{
    if constexpr (Type<T>::kind == TypeKind::Scalar)
        return std::is_same_v<T, bool>;
    else if constexpr (Type<T>::kind == TypeKind::Enum)
        return false;
    else if constexpr (Type<T>::kind == TypeKind::Array)
        return hasBool<typename detail::ArrayInfo<T>::Element>();
    else
        return membersHaveBool<T>(std::make_index_sequence<Type<T>::memberCount>{});
}

template <class T>
[[nodiscard]] bool validValue(Reader& reader) noexcept;

template <class T, std::size_t... I>
[[nodiscard]] bool validMembers(Reader& reader, std::index_sequence<I...>) noexcept
{
    return (true && ... && validValue<typename Type<T>::template Member<I>::Native>(reader));
}

template <class T>
[[nodiscard]] bool validValue(Reader& reader) noexcept
{
    if constexpr (!hasBool<T>()) {
        reader.cursor += wireSize<T>;
        return true;
    } else if constexpr (Type<T>::kind == TypeKind::Scalar) {
        return std::to_integer<std::uint8_t>(reader.bytes[reader.cursor++]) <= 1;
    } else if constexpr (Type<T>::kind == TypeKind::Array) {
        for (std::size_t i = 0; i < detail::ArrayInfo<T>::count; ++i) {
            if (!validValue<typename detail::ArrayInfo<T>::Element>(reader)) return false;
        }
        return true;
    } else {
        return validMembers<T>(reader,
                               std::make_index_sequence<Type<T>::memberCount>{});
    }
}

} // namespace codec_detail

// Exact length is checked before the first byte is accessed. A bad bool is
// rejected before an object starts lifetime or a caller's callback can run.
template <class T>
[[nodiscard]] CodecStatus encode(const T& value, std::span<std::byte> output) noexcept
{
    static_assert(Type<T>::kind != TypeKind::Void, "Void has no encoded object");
    static_assert(!std::is_volatile_v<T>, "Volatile objects require an application snapshot");
    if (output.size() != wireSize<T>) return CodecStatus::LengthMismatch;
    if (buffersOverlap(std::as_bytes(std::span<const T>{&value, 1}), output))
        return CodecStatus::Overlap;

    codec_detail::Writer writer{output};
    codec_detail::encodeValue(value, writer);
    return writer.cursor == output.size() ? CodecStatus::Ok : CodecStatus::InvalidState;
}

template <class T>
[[nodiscard]] CodecStatus decode(std::span<const std::byte> input,
                                 typename Workspace::Lease<T>& lease,
                                 T*& output) noexcept
{
    static_assert(Type<T>::kind != TypeKind::Void, "Void has no decoded object");
    output = nullptr;
    if (input.size() != wireSize<T>) return CodecStatus::LengthMismatch;
    if (buffersOverlap(input, lease.workspaceStorage())) return CodecStatus::Overlap;
    if (!lease.valid()) return CodecStatus::WorkspaceTooSmall;
    if (lease.constructed()) return CodecStatus::InvalidState;

    codec_detail::Reader validator{input};
    if (!codec_detail::validValue<T>(validator)) return CodecStatus::InvalidValue;
    if (validator.cursor != input.size()) return CodecStatus::InvalidState;

    codec_detail::Reader reader{input};
    if constexpr (std::is_trivially_default_constructible_v<T>) {
        output = lease.constructDefault();
    } else {
        output = lease.constructFrom([]() noexcept {
            return codec_detail::neutralValue<T>();
        });
    }
    codec_detail::decodeInto(*output, reader);
    if (reader.cursor != input.size()) {
        output = nullptr;
        return CodecStatus::InvalidState;
    }
    return CodecStatus::Ok;
}

} // namespace telemetry::structured

#endif
