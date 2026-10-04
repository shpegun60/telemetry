/**
 * @file TelemetrySlotTraits.h
 * @brief Compile-time recognition of callable slots, without runtime modes.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see ../LICENSE.
 */

/*
 * Recognizes callable-slot families without including their implementations.
 *
 * Reflection and binding adapters use these compile-time traits to choose the
 * correct signature and storage strategy. Keeping forward declarations here
 * avoids coupling simple function/context slots to the delegate implementation;
 * no runtime mode or state is introduced.
 */

#ifndef TELEMETRY_SLOT_TRAITS_H
#define TELEMETRY_SLOT_TRAITS_H
#pragma once
#include <cstddef>
#include <type_traits>

namespace telemetry {
template<class Signature>
class FunctionSlot;
template<class Signature>
class ContextFunctionSlot;
template<class Signature>
class DelegateRefSlot;
template<class Signature, std::size_t Bytes, std::size_t Align>
class DelegateSlot;

namespace detail {
// Recognizes callable-slot families at compile time.
template<class T>
struct IsCallableSlot : std::false_type {};

// Recognizes callable-slot families at compile time.
template<class S>
struct IsCallableSlot<FunctionSlot<S>> : std::true_type {};

// Recognizes callable-slot families at compile time.
template<class S>
struct IsCallableSlot<ContextFunctionSlot<S>> : std::true_type {};

// Recognizes callable-slot families at compile time.
template<class S>
struct IsCallableSlot<DelegateRefSlot<S>> : std::true_type {};

// Recognizes callable-slot families at compile time.
template<class S, std::size_t B, std::size_t A>
struct IsCallableSlot<DelegateSlot<S, B, A>> : std::true_type {};
template<class T>
inline constexpr bool isCallableSlot = IsCallableSlot<std::remove_cv_t<T>>::value;
} // namespace detail
} // namespace telemetry
#endif
