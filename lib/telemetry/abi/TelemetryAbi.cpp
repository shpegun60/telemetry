/**
 * @file TelemetryAbi.cpp
 * @brief Link anchor for the exact telemetry definition ABI tag.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see ../LICENSE.
 */
#include "TelemetryAbi.h"

namespace telemetry {
namespace detail {

void requireTelemetryAbi(CurrentAbiTag) noexcept {}

} // namespace detail
} // namespace telemetry
