/*
 * @file Operation.hpp
 * @brief Transport-independent encoded endpoint operations.
 * @author Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT
 */
#ifndef TELEMETRY_CORE_OPERATION_HPP
#define TELEMETRY_CORE_OPERATION_HPP
#pragma once

#include <cstdint>

namespace telemetry {
// The operation selects the endpoint family and gives endpointStatus its
// meaning. Existing transport enums retain their own public types and values.
enum class Operation : std::uint8_t {
	FieldWrite = 1,
	Command = 2,
	Service = 3,
	FieldRead = 4
};
} // namespace telemetry

#endif // TELEMETRY_CORE_OPERATION_HPP
