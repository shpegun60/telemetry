/*
 * @file Adapter.cpp
 * @brief Compiled structured Field, Command and Service dispatchers.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */

/*
 * Compiled dispatch boundary for all three encoded endpoint families.
 *
 * Each symbol includes the caller's layout tag and forwards to the family's
 * checked runtime index. This keeps the cross-module compatibility check out
 * of native typed calls while sharing the same endpoint implementations.
 */

#include "Adapter.hpp"
#include <telemetry/encoded/Dispatcher.hpp>

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
                                     std::span<const std::byte> input, std::span<std::byte> output,
                                     Workspace& workspace, detail::CurrentStructuredAbiTag) noexcept
{
	return model.services.callEncoded(id, input, output, workspace);
}

EncodedDispatchResult dispatchEncoded(ModelView model, Operation operation, PackedId endpointId,
                                      std::span<const std::byte> input, std::span<std::byte> output,
                                      Workspace& workspace, detail::CurrentStructuredAbiTag,
                                      detail::CurrentEncodedDispatchLayoutTag) noexcept
{
	EncodedDispatchResult result{operation, DispatchStatus::InvalidPayload};
	switch (operation) {
		case Operation::FieldWrite: {
			const auto value = writeFieldEncoded(model, endpointId, input, workspace);
			result.dispatch = value.dispatch;
			result.endpointStatus = static_cast<std::uint8_t>(value.endpointStatus);
			break;
		}
		case Operation::Command: {
			const auto value = executeCommandEncoded(model, endpointId, input, workspace);
			result.dispatch = value.dispatch;
			result.endpointStatus = static_cast<std::uint8_t>(value.endpointStatus);
			break;
		}
		case Operation::Service: {
			const auto value = callServiceEncoded(model, endpointId, input, output, workspace);
			result.dispatch = value.dispatch;
			result.endpointStatus = static_cast<std::uint8_t>(value.endpointStatus);
			if (value.dispatch == DispatchStatus::Ok && value.endpointStatus == ServiceStatus::Ok)
				result.written = value.written;
			break;
		}
		case Operation::FieldRead: {
			if (!input.empty())
				break;
			const auto value = readFieldEncoded(model, endpointId, output, workspace);
			result.dispatch = value.dispatch;
			if (value.dispatch == DispatchStatus::Ok)
				result.written = value.written;
			break;
		}
	}
	if (result.dispatch != DispatchStatus::Ok) {
		result.endpointStatus = 0;
		result.written = 0;
	}
	return result;
}

} // namespace telemetry
