/*
 * @file Dispatch.hpp
 * @brief Result of routing and encoding a structured endpoint call.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */

#ifndef TELEMETRY_STRUCTURED_RESULT_DISPATCH_HPP
#define TELEMETRY_STRUCTURED_RESULT_DISPATCH_HPP

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

struct EncodedCallResult {
    DispatchStatus dispatch = DispatchStatus::InternalError;
    ServiceStatus endpointStatus = ServiceStatus::Ok;
    std::uint32_t written = 0;
};

} // namespace telemetry

#endif
