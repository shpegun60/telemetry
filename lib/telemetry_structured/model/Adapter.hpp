/*
 * @file Adapter.hpp
 * @brief Compiled encoded endpoint boundary with an exact in-memory ABI tag.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */

#ifndef TELEMETRY_STRUCTURED_MODEL_ADAPTER_HPP
#define TELEMETRY_STRUCTURED_MODEL_ADAPTER_HPP

#include "../abi/StructuredAbi.hpp"

#include <span>

namespace telemetry::structured {

[[nodiscard]] EncodedReadResult readFieldEncoded(
    ModelView model, telemetry::PackedId id, std::span<std::byte> output,
    Workspace& workspace, detail::CurrentStructuredAbiTag) noexcept;

[[nodiscard]] EncodedWriteResult writeFieldEncoded(
    ModelView model, telemetry::PackedId id, std::span<const std::byte> input,
    Workspace& workspace, detail::CurrentStructuredAbiTag) noexcept;

[[nodiscard]] EncodedCommandResult executeCommandEncoded(
    ModelView model, telemetry::PackedId id, std::span<const std::byte> input,
    Workspace& workspace, detail::CurrentStructuredAbiTag) noexcept;

[[nodiscard]] inline EncodedReadResult readFieldEncoded(
    ModelView model, telemetry::PackedId id, std::span<std::byte> output, Workspace& workspace) noexcept
{
    return readFieldEncoded(model, id, output, workspace, detail::CurrentStructuredAbiTag{});
}

[[nodiscard]] inline EncodedWriteResult writeFieldEncoded(
    ModelView model, telemetry::PackedId id, std::span<const std::byte> input, Workspace& workspace) noexcept
{
    return writeFieldEncoded(model, id, input, workspace, detail::CurrentStructuredAbiTag{});
}

[[nodiscard]] inline EncodedCommandResult executeCommandEncoded(
    ModelView model, telemetry::PackedId id, std::span<const std::byte> input, Workspace& workspace) noexcept
{
    return executeCommandEncoded(model, id, input, workspace, detail::CurrentStructuredAbiTag{});
}

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
