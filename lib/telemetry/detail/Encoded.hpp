/*
 * @file Encoded.hpp
 * @brief Internal codec steps after an endpoint has checked its byte spans.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#ifndef TELEMETRY_STRUCTURED_DETAIL_ENCODED_HPP
#define TELEMETRY_STRUCTURED_DETAIL_ENCODED_HPP

#include "../codec/Codec.hpp"
#include "../codec/StoragePolicy.hpp"
#include <telemetry/core/Compiler.hpp>
#include <memory>

namespace telemetry::detail {

template <class T>
[[nodiscard]] TELEMETRY_FORCE_INLINE bool validEndpoint(std::span<const std::byte> input) noexcept
{
    if constexpr (codec_detail::hasBool<T>()) {
        codec_detail::Reader validator{input};
        return codec_detail::validValue<T>(validator);
    } else {
        return true;
    }
}

// The caller validates representation before starting object lifetime. A
// plain T{} could execute application DMI; neutralValue follows exactly the
// same construction rules as the Workspace decoder.
template <class T>
[[nodiscard]] TELEMETRY_FORCE_INLINE T decodeLocalEndpoint(
    std::span<const std::byte> input) noexcept
{
    static_assert(localObject<T>, "Local decode exceeds the configured object budget");
    if constexpr (std::is_trivially_default_constructible_v<T>) {
        // Every semantic member is overwritten before use. Match the lease
        // decoder's default-initialization; T{} would add a redundant memset
        // for arrays when -Os keeps decodeInto out of line.
        T value;
        codec_detail::Reader reader{input};
        codec_detail::decodeInto(value, reader);
        return value;
    } else {
        T value = codec_detail::neutralValue<T>();
        codec_detail::Reader reader{input};
        codec_detail::decodeInto(value, reader);
        return value;
    }
}

// Only endpoint thunks call these helpers. The caller has established the
// exact input/output size, disjoint scratch and a fresh valid lease. Calling
// the public codec here would repeat those checks inside every operation.
// Public encode/decode retain their complete standalone validation contract.
template <class T>
[[nodiscard]] TELEMETRY_FORCE_INLINE T* decodeEndpoint(
    std::span<const std::byte> input, Workspace::Lease<T>& lease) noexcept
{
    if (!validEndpoint<T>(input)) return nullptr;

    T* value;
    if constexpr (std::is_trivially_default_constructible_v<T>)
        value = lease.constructDefault();
    else
        value = lease.constructFrom([]() noexcept { return codec_detail::neutralValue<T>(); });

    codec_detail::Reader reader{input};
    codec_detail::decodeInto(*value, reader);
    return value;
}

template <class T>
TELEMETRY_FORCE_INLINE void encodeEndpoint(const T& value, std::span<std::byte> output) noexcept
{
    codec_detail::Writer writer{output};
    codec_detail::encodeValue(value, writer);
}

// Owning storage is already disjoint from output at the checked boundary.
// A borrowed object's address becomes known only after its getter/callback;
// reject overlap with all its native bytes (including padding) before writing.
template <class T>
[[nodiscard]] bool encodeBorrowedEndpoint(const T& value,
                                          std::span<std::byte> output) noexcept
{
    if (buffersOverlap(std::as_bytes(std::span<const T>{std::addressof(value), 1}), output))
        return false;
    encodeEndpoint(value, output);
    return true;
}

} // namespace telemetry::detail

#endif
