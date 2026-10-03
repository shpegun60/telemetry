/*
 * @file Exchange.hpp
 * @brief Complete packet dispatch over an already agreed structured model.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#ifndef EXAMPLE_STRUCTURED_PROTOCOL_EXCHANGE_HPP
#define EXAMPLE_STRUCTURED_PROTOCOL_EXCHANGE_HPP

#include "Binding.hpp"

namespace example::structured_protocol {

inline constexpr std::uint32_t exchangeHeaderBytes = 24;
enum class Operation : std::uint8_t { FieldWrite = 1, Command = 2, Service = 3 };

class Exchange {
public:
    // A whole request is required. Response may overlap it, including partial
    // overlap: routing and native request decoding finish before response writes.
    // Calling again executes again; requestId is correlation, not deduplication.
    [[nodiscard]] static PacketResult process(const Binding& peer, Input request,
        Output response, telemetry::structured::Workspace& workspace) noexcept
    {
        return processImpl(peer, request, response, workspace, detail::CurrentExchangeAbiTag{});
    }

private:
    [[nodiscard]] static PacketResult processImpl(const Binding&, Input, Output,
        telemetry::structured::Workspace&, detail::CurrentExchangeAbiTag) noexcept;
};

} // namespace example::structured_protocol
#endif
