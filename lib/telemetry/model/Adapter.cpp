/*
 * @file Adapter.cpp
 * @brief Compiled structured Field, Command and Service dispatchers.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */

#include "Adapter.hpp"

namespace telemetry {

EncodedReadResult readFieldEncoded(ModelView model, telemetry::PackedId id,
                                  std::span<std::byte> output, Workspace& workspace,
                                  detail::CurrentStructuredAbiTag) noexcept
{
    return model.fields.readEncoded(id, output, workspace);
}

EncodedWriteResult writeFieldEncoded(ModelView model, telemetry::PackedId id,
                                    std::span<const std::byte> input, Workspace& workspace,
                                    detail::CurrentStructuredAbiTag) noexcept
{
    return model.fields.writeEncoded(id, input, workspace);
}

EncodedCommandResult executeCommandEncoded(ModelView model, telemetry::PackedId id,
                                          std::span<const std::byte> input, Workspace& workspace,
                                          detail::CurrentStructuredAbiTag) noexcept
{
    return model.commands.executeEncoded(id, input, workspace);
}

EncodedCallResult callServiceEncoded(ModelView model, telemetry::PackedId id,
                                     std::span<const std::byte> input,
                                     std::span<std::byte> output,
                                     Workspace& workspace,
                                     detail::CurrentStructuredAbiTag) noexcept
{
    return model.services.callEncoded(id, input, output, workspace);
}

} // namespace telemetry
