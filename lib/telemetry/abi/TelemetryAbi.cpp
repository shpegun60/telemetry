/**
 * @file TelemetryAbi.cpp
 * @brief Link anchor for the exact telemetry definition ABI tag.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see ../LICENSE.
 */
#include "TelemetryAbi.h"

namespace telemetry {
namespace detail {

// Exactly one configured layout is exported by this translation unit. There
// is no runtime comparison: the linker resolves the full AbiTag specialization.
void requireTelemetryAbi(CurrentAbiTag) noexcept {}

} // namespace detail
} // namespace telemetry
