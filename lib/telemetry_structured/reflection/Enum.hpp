/*
 * @file Enum.hpp
 * @brief Stable enum-reflection customization point for the structured facade.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */

#ifndef TELEMETRY_STRUCTURED_ENUM_HPP
#define TELEMETRY_STRUCTURED_ENUM_HPP

namespace telemetry::structured::reflection {

// Specialize before first use of E to supply a complete structural dictionary.
// An explicit dictionary replaces automatic discovery; it is never merged
// with it. Stage 03 implements enumCodes()/enumEntries() and normalized Enum.
template <class E>
struct EnumReflection {};

// Planned contract: Underlying, entryCount, entryValue<I>(), entryName<I>().
// It is intentionally incomplete until Stage 03 installs validation, sorting
// and the automatic/explicit backend selection together.
template <class E>
struct Enum;

} // namespace telemetry::structured::reflection

#endif
