/*
 * @file Adapter.hpp
 * @brief Compiled encoded-Service boundary with an exact in-memory ABI tag.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */

#ifndef TELEMETRY_STRUCTURED_MODEL_ADAPTER_HPP
#define TELEMETRY_STRUCTURED_MODEL_ADAPTER_HPP

#include "../abi/StructuredAbi.hpp"

#include <span>

namespace telemetry::structured {

// The compiled symbol includes the full layout tag. An application and a
// separately built adapter with different view layouts cannot link together.
[[nodiscard]] EncodedCallResult callServiceEncoded(
    ModelView model, telemetry::PackedId id, std::span<const std::byte> input,
    std::span<std::byte> output, Workspace& workspace,
    detail::CurrentStructuredAbiTag) noexcept;

[[nodiscard]] inline EncodedCallResult callServiceEncoded(
    ModelView model, telemetry::PackedId id, std::span<const std::byte> input,
    std::span<std::byte> output, Workspace& workspace) noexcept
{
    return callServiceEncoded(model, id, input, output, workspace,
                              detail::CurrentStructuredAbiTag{});
}

} // namespace telemetry::structured

#endif
