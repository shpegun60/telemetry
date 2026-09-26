/**
 * @file TelemetryOwnerSlot.h
 * @brief Stable, non-owning slot for binding runtime objects to constant tables.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see ../LICENSE.
 */
#ifndef TELEMETRY_OWNER_SLOT_H
#define TELEMETRY_OWNER_SLOT_H

#include "../detail/TelemetryOwner.h"
#include "../detail/TelemetryTarget.h"
#include <memory>
#include <type_traits>

namespace telemetry {

// The slot and its bound object are borrowed. Keep the slot at a stable address,
// and reset/rebind it before destroying its object. Bind/reset and telemetry
// access must be externally serialized; this pointer provides no synchronization.
// There is deliberately no dereference operator: only checked adapters invoke it.
template <class T>
class OwnerSlot {
    static_assert(std::is_object_v<T> && !std::is_array_v<T> && !std::is_volatile_v<T>,
                  "OwnerSlot requires a non-volatile object type");
    T* owner_ = nullptr;
public:
    using Owner = T;
    constexpr OwnerSlot() noexcept = default;
    OwnerSlot(const OwnerSlot&) = delete;
    OwnerSlot(OwnerSlot&&) = delete;
    OwnerSlot& operator=(const OwnerSlot&) = delete;
    OwnerSlot& operator=(OwnerSlot&&) = delete;

    constexpr void bind(T& owner) noexcept { owner_ = std::addressof(owner); }
    // Only actual cv/base lvalues may be borrowed; conversions may construct
    // a temporary even when their source is an lvalue. Braces need separate
    // overloads because a forwarding reference cannot deduce {} or {proxy}.
    template <class U, std::enable_if_t<!detail::isBorrowedObjectArgument<T, U>, int> = 0>
    void bind(U&&) = delete;
    void bind(T&&) = delete;
    template <class U, std::enable_if_t<!detail::isBorrowedObjectArgument<T, U&>, int> = 0>
    void bind(std::initializer_list<U>&&) = delete;
    constexpr void reset() noexcept { owner_ = nullptr; }
    [[nodiscard]] constexpr T* get() const noexcept { return owner_; }
    [[nodiscard]] constexpr bool available() const noexcept { return detail::pointerPresent(owner_); }
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return available(); }
};

namespace detail {
template <class T> struct IsOwnerSlot : std::false_type {};
template <class T> struct IsOwnerSlot<OwnerSlot<T>> : std::true_type {};
template <class T> inline constexpr bool isOwnerSlot = IsOwnerSlot<std::remove_cv_t<T>>::value;
} // namespace detail
} // namespace telemetry
#endif
