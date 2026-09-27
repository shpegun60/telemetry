/*
 * @file Bind.hpp
 * @brief One-time version and descriptor-fingerprint agreement for one peer.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#ifndef RESOURCE_STRUCTURED_BIND_HPP
#define RESOURCE_STRUCTURED_BIND_HPP

#include "Binding.hpp"

namespace resource::structured {

inline constexpr std::uint32_t bindRequestBytes = 16;
inline constexpr std::uint32_t bindResponseBytes = 8;
enum class BindStatus : std::uint8_t {
    Ready = 0, SchemaMismatch = 1, UnsupportedVersion = 2, InvalidRequest = 3
};

class Bind {
public:
    // model must describe the same immutable schema as expectedFingerprint.
    // Keep the named ModelView alive until reset/disconnect; temporary views,
    // braces and conversion proxies cannot silently create borrowed state.
    template <class... Explicit, class View>
        requires (sizeof...(Explicit) == 0 &&
                  (std::same_as<View, telemetry::structured::ModelView&> ||
                   std::same_as<View, const telemetry::structured::ModelView&>))
    [[nodiscard]] static PacketResult process(Binding& peer, View&& model,
        std::uint64_t expectedFingerprint, Input request, Output response) noexcept
    {
        return processImpl(peer, model, expectedFingerprint, request, response,
                           detail::CurrentExchangeAbiTag{});
    }

private:
    [[nodiscard]] static PacketResult processImpl(Binding&, const telemetry::structured::ModelView&,
        std::uint64_t, Input, Output, detail::CurrentExchangeAbiTag) noexcept;
};

} // namespace resource::structured
#endif
