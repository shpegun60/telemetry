/*
 * @file Dispatch.hpp
 * @brief Result of routing and encoding a structured endpoint call.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */

/*
 * Reports routing and codec outcomes independently of Service application status.
 *
 * Dispatch failure means the encoded operation could not deliver its normal
 * endpoint result. A successful dispatch may still carry an application refusal.
 * Packet framing, versions and connection state belong to the surrounding
 * protocol, while written counts describe only the emitted payload.
 */

#ifndef TELEMETRY_RESULT_DISPATCH_HPP
#define TELEMETRY_RESULT_DISPATCH_HPP
#pragma once

#include "ServiceResult.hpp"

#include <cstdint>

namespace telemetry {

// Routing/codec failures are separate from a ServiceStatus returned by the
// application. Connections, packet versions and agreement state belong to
// the caller's protocol. Existing endpoint codes keep their numeric values.
enum class DispatchStatus : std::uint8_t {
	Ok = 0,
	NotFound = 4,
	InvalidPayload = 5,
	BufferTooSmall = 6,
	WorkspaceTooSmall = 7,
	InternalError = 8,
	Unavailable = 9
};

// endpointStatus and written describe the Service outcome when dispatch is Ok.
// Otherwise the callback result was not delivered and no response payload is
// reported. Service refusals can be successful dispatches with written == 0.
struct EncodedCallResult {
	DispatchStatus dispatch = DispatchStatus::InternalError;
	ServiceStatus endpointStatus = ServiceStatus::Ok;
	std::uint32_t written = 0;
};

} // namespace telemetry

#endif
