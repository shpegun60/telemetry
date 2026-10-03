/*
 * @file EndpointResults.hpp
 * @brief Distinct encoded Field and Command operation results.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#ifndef TELEMETRY_STRUCTURED_RESULT_ENDPOINT_RESULTS_HPP
#define TELEMETRY_STRUCTURED_RESULT_ENDPOINT_RESULTS_HPP

#include "Dispatch.hpp"
#include <telemetry/result/EndpointStatus.hpp>

namespace telemetry {

struct EncodedReadResult {
    DispatchStatus dispatch = DispatchStatus::InternalError;
    std::uint32_t written = 0;
};

// A register-sized result avoids byte-by-byte aggregate return assembly on
// ARM GCC. This is an in-process result layout, not a wire representation.
struct alignas(std::uint32_t) EncodedWriteResult {
    DispatchStatus dispatch = DispatchStatus::InternalError;
    telemetry::WriteResult endpointStatus = telemetry::WriteResult::Applied;
};

struct alignas(std::uint32_t) EncodedCommandResult {
    DispatchStatus dispatch = DispatchStatus::InternalError;
    telemetry::CommandResult endpointStatus = telemetry::CommandResult::Executed;
};

namespace model_detail {
constexpr bool validStatus(telemetry::WriteResult value) noexcept
{
    switch (value) {
    case telemetry::WriteResult::Applied:
    case telemetry::WriteResult::NotFound:
    case telemetry::WriteResult::ReadOnly:
    case telemetry::WriteResult::InvalidValue:
    case telemetry::WriteResult::Busy:
    case telemetry::WriteResult::Unavailable: return true;
    }
    return false;
}

constexpr bool validStatus(telemetry::CommandResult value) noexcept
{
    switch (value) {
    case telemetry::CommandResult::Executed:
    case telemetry::CommandResult::Accepted:
    case telemetry::CommandResult::NotFound:
    case telemetry::CommandResult::Unavailable:
    case telemetry::CommandResult::ArgumentCountMismatch:
    case telemetry::CommandResult::InvalidValue:
    case telemetry::CommandResult::Busy:
    case telemetry::CommandResult::Failed: return true;
    }
    return false;
}
} // namespace model_detail

} // namespace telemetry

#endif
