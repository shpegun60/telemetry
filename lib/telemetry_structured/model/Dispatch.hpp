/*
 * @file Dispatch.hpp
 * @brief Result of routing and encoding a structured endpoint call.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */

#ifndef TELEMETRY_STRUCTURED_MODEL_DISPATCH_HPP
#define TELEMETRY_STRUCTURED_MODEL_DISPATCH_HPP

#include "../result/ServiceResult.hpp"

#include <cstdint>

namespace telemetry::structured {

// Routing/codec failures are separate from a ServiceStatus returned by the
// application. These values are reserved for the eventual exchange envelope.
enum class DispatchStatus : std::uint8_t {
    Ok = 0,
    InvalidRequest = 1,
    UnsupportedVersion = 2,
    NotReady = 3,
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

} // namespace telemetry::structured

#endif
