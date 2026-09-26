/**
 * @file TelemetryOwner.h
 * @brief Compile-time checks for borrowing actual objects at stable addresses.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see ../LICENSE.
 */
#ifndef TELEMETRY_DETAIL_OWNER_H
#define TELEMETRY_DETAIL_OWNER_H

#include <initializer_list>
#include <type_traits>

namespace telemetry::detail {

template <class> struct MemberOwner {};
template <class Member, class Owner>
struct MemberOwner<Member Owner::*> { using Type = Owner; };

// std::invoke also accepts pointers, smart pointers and reference_wrapper.
// A descriptor borrows the object itself; those indirections must be explicit
// (*pointer) or provided by OwnerSlot, whose adapter checks availability.
template <class Member, class Owner, class = void>
struct IsDirectMemberOwner : std::false_type {};
template <class Member, class Owner>
struct IsDirectMemberOwner<Member, Owner, std::void_t<typename MemberOwner<Member>::Type>>
    : std::is_base_of<typename MemberOwner<Member>::Type, std::remove_cv_t<Owner>> {};
template <class Member, class Owner>
inline constexpr bool isDirectMemberOwner = IsDirectMemberOwner<Member, Owner>::value;

// An explicitly supplied const T must not hide a proxy conversion that creates
// a temporary T. Pointer convertibility permits cv/base adjustment only, never
// user-defined conversions. Argument is deduced independently of Expected.
template <class Expected, class Argument, class = void>
struct IsBorrowedObjectArgument : std::false_type {};
template <class Expected, class Argument>
struct IsBorrowedObjectArgument<Expected, Argument,
    std::void_t<Expected*, std::remove_reference_t<Argument>*>>
    : std::bool_constant<std::is_lvalue_reference_v<Argument>
        && (std::is_void_v<Expected>
            || std::is_convertible_v<std::remove_reference_t<Argument>*, Expected*>)> {};
template <class Expected, class Argument>
inline constexpr bool isBorrowedObjectArgument = IsBorrowedObjectArgument<Expected, Argument>::value;

// Deduction of initializer_list elements decays a named array to a pointer.
// Context adapters may borrow arrays, so do not let the proxy guard hide the
// ordinary reference binding of {namedArray}. Typed rvalue-array overloads
// still reject initializer lists that would create new array storage.
template <class Expected, class Element>
inline constexpr bool isBorrowedContextListElement =
    isBorrowedObjectArgument<Expected, Element&>
    || (std::is_array_v<Expected> && std::is_pointer_v<Element>
        && std::is_convertible_v<Element, std::add_pointer_t<std::remove_extent_t<Expected>>>);

} // namespace telemetry::detail
#endif
