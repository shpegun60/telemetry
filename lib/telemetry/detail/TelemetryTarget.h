/**
 * @file TelemetryTarget.h
 * @brief Address-independent validation of compile-time callback targets.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see ../LICENSE.
 */
#ifndef TELEMETRY_DETAIL_TARGET_H
#define TELEMETRY_DETAIL_TARGET_H

#include <type_traits>

namespace telemetry::detail {

// Compare template argument identities, not addresses. GCC may treat function
// addresses as potentially zero with -fno-delete-null-pointer-checks (also used
// by some sanitizer configurations), but the target's template identity is
// still a constant expression. Runtime function pointers retain normal checks.
template <auto Target>
inline constexpr bool nonNullTarget = [] {
    using T = decltype(Target);
    if constexpr (std::is_pointer_v<T> || std::is_member_pointer_v<T>) {
        return !std::is_same_v<std::integral_constant<T, Target>,
                               std::integral_constant<T, nullptr>>;
    } else {
        return false;
    }
}();

} // namespace telemetry::detail

#endif
