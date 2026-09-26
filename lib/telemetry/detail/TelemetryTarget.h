/**
 * @file TelemetryTarget.h
 * @brief Validation of public compile-time callback targets.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see ../LICENSE.
 */
#ifndef TELEMETRY_DETAIL_TARGET_H
#define TELEMETRY_DETAIL_TARGET_H

#include <type_traits>

namespace telemetry::detail {

// A named GNU weak function can still resolve to nullptr. Template identity
// alone therefore cannot prove that a public target exists. Require an actual
// constant nonnull address; an unresolved weak declaration is not eligible.
// Affected GCC versions also reject ordinary function targets
// with -fno-delete-null-pointer-checks. Private factory adapters use a separate
// known-defined construction path; public targets keep this exact check.
template <auto Target>
inline constexpr bool nonNullTarget = [] {
    using T = decltype(Target);
    if constexpr (std::is_pointer_v<T> || std::is_member_pointer_v<T>) {
        return Target != nullptr;
    } else {
        return false;
    }
}();

} // namespace telemetry::detail

#endif
