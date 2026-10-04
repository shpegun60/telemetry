/*
 * @file StructuredAbi.cpp
 * @brief Definition of the one accepted structured descriptor ABI tag.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */

/*
 * Defines the only ABI-tag specialization accepted by this compiled library.
 *
 * A caller built with different layout facts requests a different symbol and
 * cannot silently exchange incompatible views. Keep this object with compiled
 * adapters; header-only native calls do not need it.
 */

#include "StructuredAbi.hpp"

namespace telemetry::detail {

template<>
void requireStructuredAbi<CurrentStructuredAbiTag>(CurrentStructuredAbiTag) noexcept
{}

} // namespace telemetry::detail
