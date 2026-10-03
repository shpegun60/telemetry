/*
 * @file Binding.hpp
 * @brief Transport-owned agreement with a stable immutable structured model.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#ifndef EXAMPLE_STRUCTURED_PROTOCOL_BINDING_HPP
#define EXAMPLE_STRUCTURED_PROTOCOL_BINDING_HPP

#include <cstddef>
#include <cstdint>
#include <span>
#include <telemetry/abi/StructuredAbi.hpp>

namespace example::structured_protocol {

using Input = std::span<const std::byte>;
using Output = std::span<std::byte>;
inline constexpr std::uint16_t protocolMajor = 3;
inline constexpr std::uint16_t protocolMinor = 0;

// This example owns packet/session errors. Telemetry's DispatchStatus covers
// endpoint operations only; it has no Ready, connection or version state.
enum class PacketStatus : std::uint8_t {
    Ok = 0, InvalidRequest = 1, UnsupportedVersion = 2, NotReady = 3,
    NotFound = 4, InvalidPayload = 5, BufferTooSmall = 6,
    WorkspaceTooSmall = 7, InternalError = 8, Unavailable = 9
};

class Bind;
class Exchange;

// One context per admitted transport peer, including peers not yet bound.
// The transport owns its capacity, synchronization, queue reset and lifetime.
// ModelView itself, and every table/owner it borrows, must outlive Ready.
class Binding {
public:
    constexpr Binding() noexcept = default;
    Binding(const Binding&) = delete;
    Binding& operator=(const Binding&) = delete;
    Binding(Binding&&) = delete;
    Binding& operator=(Binding&&) = delete;

    [[nodiscard]] constexpr bool ready() const noexcept { return model_ != nullptr; }
    constexpr void reset() noexcept { model_ = nullptr; }

private:
    friend class Bind;
    friend class Exchange;
    const telemetry::ModelView* model_ = nullptr;
};

// Local result of producing a packet. Only [0,written) may be sent. Endpoint
// status lives in the response envelope, distinct from routing/preflight errors.
struct PacketResult {
    std::uint32_t written = 0;
    PacketStatus dispatch = PacketStatus::InternalError;

    constexpr PacketResult(PacketStatus status,
                           std::uint32_t bytes = 0) noexcept : written(bytes), dispatch(status) {}
};

namespace detail {
static_assert(std::is_standard_layout_v<Binding> && sizeof(Binding) == sizeof(void*));
static_assert(std::is_standard_layout_v<PacketResult>);
template <class CoreTag, std::size_t... Parts> struct ExchangeAbiTag {};
using CurrentExchangeAbiTag = ExchangeAbiTag<telemetry::detail::CurrentStructuredAbiTag,
    1, sizeof(Binding), alignof(Binding), sizeof(PacketResult), alignof(PacketResult),
    offsetof(PacketResult, written), offsetof(PacketResult, dispatch)>;
} // namespace detail

} // namespace example::structured_protocol
#endif
