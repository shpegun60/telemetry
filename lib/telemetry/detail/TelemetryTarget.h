/**
 * @file TelemetryTarget.h
 * @brief Validation of public compile-time callback targets.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see ../LICENSE.
 */
#ifndef TELEMETRY_DETAIL_TARGET_H
#define TELEMETRY_DETAIL_TARGET_H

#include "../core/TelemetryCompiler.h"
#include <type_traits>

namespace telemetry::detail {

// Compare template identities, not function addresses, in constant evaluation.
// GCC cannot always fold an address/null comparison with
// -fno-delete-null-pointer-checks. A weak symbol can still resolve to null at
// link time, so invocations check their target and reference-slot bindings
// resolve it before installing a delegate.
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

// For pointer values passed as ordinary constexpr arguments, GCC may be unable
// to decide whether a non-null function address compares equal to null. In
// that case keep the descriptor and let its invocation check the actual value.
// At run time, and for foldable constexpr nulls, preserve the exact test.
template <class Pointer>
constexpr bool pointerPresenceUncertain(Pointer pointer) noexcept
{
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_is_constant_evaluated()
        && !__builtin_constant_p(pointer == nullptr);
#else
    (void)pointer;
    return false;
#endif
}

template <class Pointer>
constexpr bool pointerPresent(Pointer pointer) noexcept
{
    if (pointerPresenceUncertain(pointer)) return true;
    return pointer != nullptr;
}

template <auto Target>
TELEMETRY_FORCE_INLINE constexpr bool targetAvailable() noexcept
{
    if constexpr (std::is_pointer_v<decltype(Target)>
                  || std::is_member_pointer_v<decltype(Target)>) {
        return pointerPresent(Target);
    } else {
        return true; // A structural callable object is a value, not a pointer.
    }
}

} // namespace telemetry::detail

#endif
