/*
 * @file Binding.hpp
 * @brief Transport-owned agreement with a stable immutable structured model.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#ifndef RESOURCE_STRUCTURED_BINDING_HPP
#define RESOURCE_STRUCTURED_BINDING_HPP

#include "../Types.hpp"
#include <telemetry_structured/abi/StructuredAbi.hpp>

namespace resource::structured {

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
    const telemetry::structured::ModelView* model_ = nullptr;
};

// Local result of producing a packet. Only [0,written) may be sent. Endpoint
// status lives in the response envelope, distinct from routing/preflight errors.
struct PacketResult {
    std::uint32_t written = 0;
    telemetry::structured::DispatchStatus dispatch = telemetry::structured::DispatchStatus::InternalError;

    constexpr PacketResult(telemetry::structured::DispatchStatus status,
                           std::uint32_t bytes = 0) noexcept : written(bytes), dispatch(status) {}
};

namespace detail {
static_assert(std::is_standard_layout_v<Binding> && sizeof(Binding) == sizeof(void*));
static_assert(std::is_standard_layout_v<PacketResult>);
template <class CoreTag, std::size_t... Parts> struct ExchangeAbiTag {};
using CurrentExchangeAbiTag = ExchangeAbiTag<telemetry::structured::detail::CurrentStructuredAbiTag,
    1, sizeof(Binding), alignof(Binding), sizeof(PacketResult), alignof(PacketResult),
    offsetof(PacketResult, written), offsetof(PacketResult, dispatch)>;
} // namespace detail

} // namespace resource::structured
#endif
