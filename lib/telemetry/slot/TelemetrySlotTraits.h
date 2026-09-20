/**
 * @file TelemetrySlotTraits.h
 * @brief Compile-time recognition of callable slots, without runtime modes.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see ../LICENSE.
 */
#ifndef TELEMETRY_SLOT_TRAITS_H
#define TELEMETRY_SLOT_TRAITS_H
#include <cstddef>
#include <type_traits>
namespace telemetry {
template <class Signature> class FunctionSlot;
template <class Signature> class ContextFunctionSlot;
template <class Signature> class DelegateRefSlot;
template <class Signature, std::size_t Bytes, std::size_t Align> class DelegateSlot;
namespace detail {
template <class T> struct IsCallableSlot : std::false_type {};
template <class S> struct IsCallableSlot<FunctionSlot<S>> : std::true_type {};
template <class S> struct IsCallableSlot<ContextFunctionSlot<S>> : std::true_type {};
template <class S> struct IsCallableSlot<DelegateRefSlot<S>> : std::true_type {};
template <class S, std::size_t B, std::size_t A>
struct IsCallableSlot<DelegateSlot<S, B, A>> : std::true_type {};
template <class T> inline constexpr bool isCallableSlot = IsCallableSlot<std::remove_cv_t<T>>::value;
} // namespace detail
} // namespace telemetry
#endif
