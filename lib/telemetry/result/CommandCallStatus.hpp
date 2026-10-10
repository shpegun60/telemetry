/*
 * @file CommandCallStatus.hpp
 * @brief Flat native Command routing and application outcomes.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */

#ifndef TELEMETRY_RESULT_COMMAND_CALL_STATUS_HPP
#define TELEMETRY_RESULT_COMMAND_CALL_STATUS_HPP
#pragma once

#include "EndpointStatus.hpp"
#include "NativeCallResult.hpp"

#include <cstdint>
#include <cstdlib>

namespace telemetry {

// A separate native status preserves every CommandResult and its wire codes.
// Accepted still means queued work, whose completion is application-owned.
// NotFound covers either routing or application refusal; callAs() retains the
// original distinction for callers that need both outcome layers.
// An undefined application code maps to Failed; callAs() retains its raw code.
enum class CommandCallStatus : std::uint8_t {
	Executed,
	Accepted,
	NotFound,
	Unavailable,
	ArgumentCountMismatch,
	InvalidValue,
	Busy,
	Failed,
	SignatureMismatch
};

namespace result_detail {

inline CommandCallStatus commandCallStatus(CommandResult status) noexcept
{
	switch (status) {
		case CommandResult::Executed:
			return CommandCallStatus::Executed;
		case CommandResult::Accepted:
			return CommandCallStatus::Accepted;
		case CommandResult::NotFound:
			return CommandCallStatus::NotFound;
		case CommandResult::Unavailable:
			return CommandCallStatus::Unavailable;
		case CommandResult::ArgumentCountMismatch:
			return CommandCallStatus::ArgumentCountMismatch;
		case CommandResult::InvalidValue:
			return CommandCallStatus::InvalidValue;
		case CommandResult::Busy:
			return CommandCallStatus::Busy;
		case CommandResult::Failed:
			return CommandCallStatus::Failed;
	}
	return CommandCallStatus::Failed;
}

inline CommandCallStatus commandCallStatus(const NativeCallResult<CommandResult>& result) noexcept
{
	switch (result.status()) {
		case NativeCallStatus::Ok:
			return commandCallStatus(result.value());
		case NativeCallStatus::NotFound:
			return CommandCallStatus::NotFound;
		case NativeCallStatus::SignatureMismatch:
			return CommandCallStatus::SignatureMismatch;
	}
	std::abort();
}

} // namespace result_detail
} // namespace telemetry

#endif
