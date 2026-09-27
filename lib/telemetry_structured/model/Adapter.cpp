/*
 * @file Adapter.cpp
 * @brief Compiled structured Service dispatcher.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */

#include "Adapter.hpp"

namespace telemetry::structured {

EncodedCallResult callServiceEncoded(ModelView model, telemetry::PackedId id,
                                     std::span<const std::byte> input,
                                     std::span<std::byte> output,
                                     Workspace& workspace,
                                     detail::CurrentStructuredAbiTag) noexcept
{
    return model.services.callEncoded(id, input, output, workspace);
}

} // namespace telemetry::structured
