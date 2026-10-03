/* Shared endpoint statuses. Authors: Ruslan Kovtun (shpegun60), codexAi.
 * SPDX-License-Identifier: MIT. */
#pragma once
#include <cstdint>

namespace telemetry {
enum class WriteResult : std::uint8_t {
    Applied = 0,
    NotFound,
    ReadOnly,
    InvalidValue,
    Busy,
    Unavailable, // A late-bound owner slot is empty; existing result codes stay unchanged.
};

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
