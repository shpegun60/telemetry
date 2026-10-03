/*
 * @file BorrowedServiceResult.hpp
 * @brief Service status and a const view, without response materialization.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#ifndef TELEMETRY_RESULT_BORROWED_SERVICE_RESULT_HPP
#define TELEMETRY_RESULT_BORROWED_SERVICE_RESULT_HPP

#include "BorrowedValue.hpp"
#include "ServiceResult.hpp"

namespace telemetry {

// Unlike ServiceResult<T>, this result neither owns nor destroys T. Native
// callers must keep the response alive after call() for all their view uses.
template <class T>
class BorrowedServiceResult {
public:
    using value_type = T;

    template <class... Explicit, class Value>
        requires (sizeof...(Explicit) == 0 && requires(Value&& value) {
            BorrowedValue<T>::from(std::forward<Value>(value));
        })
    [[nodiscard]] static constexpr BorrowedServiceResult success(Value&& value) noexcept
    {
        return BorrowedServiceResult{ServiceStatus::Ok,
            BorrowedValue<T>::from(std::forward<Value>(value))};
    }

    [[nodiscard]] static BorrowedServiceResult failure(ServiceStatus status) noexcept
    {
        result_detail::requireFailure(status);
        return BorrowedServiceResult{status, {}};
    }

    [[nodiscard]] constexpr ServiceStatus status() const noexcept { return status_; }
    [[nodiscard]] constexpr bool hasValue() const noexcept { return value_.hasValue(); }
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return hasValue(); }
    [[nodiscard]] constexpr const T* valueOrNull() const noexcept { return value_.valueOrNull(); }
    [[nodiscard]] constexpr const T& value() const noexcept { return value_.value(); }
    [[nodiscard]] constexpr const T& operator*() const noexcept { return value(); }
    [[nodiscard]] constexpr const T* operator->() const noexcept { return valueOrNull(); }

private:
    constexpr BorrowedServiceResult(ServiceStatus status, BorrowedValue<T> value) noexcept
        : status_(status), value_(value) {}

    ServiceStatus status_;
    BorrowedValue<T> value_;
};

} // namespace telemetry

#endif
