/*
 * @file StructuredAbi.cpp
 * @brief Definition of the one accepted structured descriptor ABI tag.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */

#include "StructuredAbi.hpp"

namespace telemetry::detail {

template <>
void requireStructuredAbi<CurrentStructuredAbiTag>(CurrentStructuredAbiTag) noexcept {}

} // namespace telemetry::detail
