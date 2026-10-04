/*
 * @file Adapter.hpp
 * @brief Compiled encoded endpoint boundary with an exact in-memory ABI tag.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */

/*
 * Public compiled entry points for encoded operations on an erased ModelView.
 *
 * Inline overloads add the caller's exact layout tag to the compiled symbol.
 * The implementation then uses checked family indexes; native typed table
 * calls remain header-only. The caller owns buffers, Workspace, referenced
 * tables and all transport-level framing or correlation.
 */

#ifndef TELEMETRY_MODEL_ADAPTER_HPP
#define TELEMETRY_MODEL_ADAPTER_HPP
#pragma once

#include "../abi/StructuredAbi.hpp"

#include <span>

namespace telemetry {

// Tagged compiled overloads are the module boundary. Use the four-/five-argument
// wrappers below in application code; they bind the caller's exact current ABI.
// ModelView, payload spans and Workspace remain borrowed for the whole call.
[[nodiscard]] EncodedReadResult readFieldEncoded(ModelView model, telemetry::PackedId id,
                                                 std::span<std::byte> output, Workspace& workspace,
                                                 detail::CurrentStructuredAbiTag) noexcept;

[[nodiscard]] EncodedWriteResult writeFieldEncoded(ModelView model, telemetry::PackedId id,
                                                   std::span<const std::byte> input,
                                                   Workspace& workspace,
                                                   detail::CurrentStructuredAbiTag) noexcept;

[[nodiscard]] EncodedCommandResult executeCommandEncoded(ModelView model, telemetry::PackedId id,
                                                         std::span<const std::byte> input,
                                                         Workspace& workspace,
                                                         detail::CurrentStructuredAbiTag) noexcept;

// Convenience wrappers introduce only the ABI tag. Checked family entries
// validate wire length and storage before any callback is invoked.
[[nodiscard]] inline EncodedReadResult readFieldEncoded(ModelView model, telemetry::PackedId id,
                                                        std::span<std::byte> output,
                                                        Workspace& workspace) noexcept
{
	return readFieldEncoded(model, id, output, workspace, detail::CurrentStructuredAbiTag{});
}

[[nodiscard]] inline EncodedWriteResult writeFieldEncoded(ModelView model, telemetry::PackedId id,
                                                          std::span<const std::byte> input,
                                                          Workspace& workspace) noexcept
{
	return writeFieldEncoded(model, id, input, workspace, detail::CurrentStructuredAbiTag{});
}

[[nodiscard]] inline EncodedCommandResult executeCommandEncoded(ModelView model,
                                                                telemetry::PackedId id,
                                                                std::span<const std::byte> input,
                                                                Workspace& workspace) noexcept
{
	return executeCommandEncoded(model, id, input, workspace, detail::CurrentStructuredAbiTag{});
}

// The compiled symbol includes the full layout tag. An application and a
// separately built adapter with different view layouts cannot link together.
[[nodiscard]] EncodedCallResult callServiceEncoded(ModelView model, telemetry::PackedId id,
                                                   std::span<const std::byte> input,
                                                   std::span<std::byte> output,
                                                   Workspace& workspace,
                                                   detail::CurrentStructuredAbiTag) noexcept;

[[nodiscard]] inline EncodedCallResult callServiceEncoded(ModelView model, telemetry::PackedId id,
                                                          std::span<const std::byte> input,
                                                          std::span<std::byte> output,
                                                          Workspace& workspace) noexcept
{
	return callServiceEncoded(model, id, input, output, workspace,
	                          detail::CurrentStructuredAbiTag{});
}

} // namespace telemetry

#endif
