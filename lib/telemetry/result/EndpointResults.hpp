/*
 * @file EndpointResults.hpp
 * @brief Distinct encoded Field and Command operation results.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */

/*
 * Encoded Field and Command results with separate dispatch and callback status.
 *
 * Endpoint status is meaningful when dispatch succeeds; it is not a replacement
 * for routing or payload validation errors. Register-sized result alignment is
 * an in-process return-layout choice. Status validation prevents an unknown
 * callback code from crossing the encoded boundary as a supported result.
 */

#ifndef TELEMETRY_RESULT_ENDPOINT_RESULTS_HPP
#define TELEMETRY_RESULT_ENDPOINT_RESULTS_HPP
#pragma once

#include "Dispatch.hpp"
#include <telemetry/result/EndpointStatus.hpp>

namespace telemetry {

// written is the number of payload bytes emitted. A successful read has no
// additional application status; unavailable getters use a dispatch failure.
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

// Encoded Command dispatch and callback outcome.
struct alignas(std::uint32_t) EncodedCommandResult {
	DispatchStatus dispatch = DispatchStatus::InternalError;
	telemetry::CommandResult endpointStatus = telemetry::CommandResult::Executed;
};

// Encoded dispatch accepts only defined callback statuses. Native calls keep
// their direct return contract; these checks guard the erased protocol boundary.
namespace model_detail {
constexpr bool validStatus(telemetry::WriteResult value) noexcept
{
	switch (value) {
		case telemetry::WriteResult::Applied:
		case telemetry::WriteResult::NotFound:
		case telemetry::WriteResult::ReadOnly:
		case telemetry::WriteResult::InvalidValue:
		case telemetry::WriteResult::Busy:
		case telemetry::WriteResult::Unavailable:
			return true;
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
		case telemetry::CommandResult::Failed:
			return true;
	}
	return false;
}
} // namespace model_detail

} // namespace telemetry

#endif
