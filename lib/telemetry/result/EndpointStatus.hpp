/* Shared endpoint statuses. Authors: Ruslan Kovtun (shpegun60), codexAi.
 * SPDX-License-Identifier: MIT. */

/*
 * Application outcomes returned by native Field setters and Command callbacks.
 *
 * These codes describe whether the owner applied, accepted or refused an
 * operation. Encoded wrappers report routing and codec failure separately.
 * Accepted means the owner queued work; the library does not establish its
 * eventual completion or maintain that queue.
 */

#ifndef TELEMETRY_LIB_TELEMETRY_RESULT_ENDPOINTSTATUS_HPP
#define TELEMETRY_LIB_TELEMETRY_RESULT_ENDPOINTSTATUS_HPP
#pragma once

#include <cstdint>

namespace telemetry {
// Setter/application outcomes. ReadOnly describes declared capability;
// Unavailable describes a temporarily absent late-bound setter or owner.
enum class WriteResult : std::uint8_t {
	Applied = 0,
	NotFound,
	ReadOnly,
	InvalidValue,
	Busy,
	Unavailable, // A late-bound owner slot is empty; existing result codes stay unchanged.
};

// A command can finish synchronously or be accepted by an application queue.
// The library carries the status but supplies no completion tracking.
enum class CommandResult : std::uint8_t {
	Executed = 0,
	Accepted, // Queued by the owner; completion has not been established.
	NotFound,
	Unavailable,
	ArgumentCountMismatch,
	InvalidValue,
	Busy,
	Failed,
};
} // namespace telemetry

#endif // TELEMETRY_LIB_TELEMETRY_RESULT_ENDPOINTSTATUS_HPP
