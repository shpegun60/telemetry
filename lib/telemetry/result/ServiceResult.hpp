/*
 * @file ServiceResult.hpp
 * @brief Status and optional service payload with explicit object lifetime.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */

#ifndef TELEMETRY_RESULT_SERVICE_RESULT_HPP
#define TELEMETRY_RESULT_SERVICE_RESULT_HPP

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

namespace telemetry {

inline constexpr std::size_t maxConvenientServiceResponseBytes = 256;

enum class ServiceStatus : std::uint8_t {
    Ok = 0,
    InvalidArgument = 1,
    Unavailable = 2,
    Busy = 3,
    Failed = 4
};

namespace result_detail {

inline void requireFailure(ServiceStatus status) noexcept
{
    if (status == ServiceStatus::Ok ||
        static_cast<std::uint8_t>(status) > static_cast<std::uint8_t>(ServiceStatus::Failed))
        std::abort();
}

} // namespace result_detail

template <class T>
class ServiceResult {
    static_assert(!std::is_void_v<T> && !std::is_reference_v<T> &&
                  !std::is_const_v<T> && !std::is_volatile_v<T>,
                  "ServiceResult payload must be a mutable object; use the void specialization");
    static_assert(std::is_nothrow_destructible_v<T>,
                  "ServiceResult payload destructor must be noexcept");

public:
    // This factory preserves prvalue construction into the result's final
    // storage. It is the bounded-stack form for large response objects.
    template <class Factory>
    [[nodiscard]] static ServiceResult successFrom(Factory&& factory)
    {
        static_assert(std::is_same_v<std::invoke_result_t<Factory&&>, T>,
                      "ServiceResult factory must return the exact response type");
        return ServiceResult{SuccessTag{}, std::forward<Factory>(factory)};
    }

    // Convenient for small responses. Supplying an already materialized large
    // T may cost a temporary; successFrom() keeps that choice explicit.
    [[nodiscard]] static ServiceResult success(T value)
    {
        static_assert(sizeof(T) <= maxConvenientServiceResponseBytes,
                      "Large service response must use successFrom(factory) to avoid a hidden stack copy");
        return successFrom([&value]() -> T { return std::move(value); });
    }

    [[nodiscard]] static ServiceResult failure(ServiceStatus status) noexcept
    {
        result_detail::requireFailure(status);
        return ServiceResult{status};
    }

    ServiceResult(const ServiceResult& other)
        : status_(other.status_)
    {
        if (other.hasValue())
            ::new (static_cast<void*>(std::addressof(payload_.value))) T(other.payload_.value);
    }

    ServiceResult(ServiceResult&& other) noexcept(std::is_nothrow_move_constructible_v<T>)
        : status_(other.status_)
    {
        if (other.hasValue())
            ::new (static_cast<void*>(std::addressof(payload_.value))) T(std::move(other.payload_.value));
    }

    ServiceResult& operator=(const ServiceResult& other)
    {
        if (this == std::addressof(other)) return *this;
        if (hasValue() && other.hasValue()) {
            payload_.value = other.payload_.value;
        } else if (hasValue()) {
            std::destroy_at(std::addressof(payload_.value));
        } else if (other.hasValue()) {
            ::new (static_cast<void*>(std::addressof(payload_.value))) T(other.payload_.value);
        }
        status_ = other.status_;
        return *this;
    }

    ServiceResult& operator=(ServiceResult&& other)
        noexcept(std::is_nothrow_move_constructible_v<T> &&
                 std::is_nothrow_move_assignable_v<T>)
    {
        if (this == std::addressof(other)) return *this;
        if (hasValue() && other.hasValue()) {
            payload_.value = std::move(other.payload_.value);
        } else if (hasValue()) {
            std::destroy_at(std::addressof(payload_.value));
        } else if (other.hasValue()) {
            ::new (static_cast<void*>(std::addressof(payload_.value))) T(std::move(other.payload_.value));
        }
        status_ = other.status_;
        return *this;
    }

    ~ServiceResult()
    {
        if (hasValue()) std::destroy_at(std::addressof(payload_.value));
    }

    [[nodiscard]] ServiceStatus status() const noexcept { return status_; }
    [[nodiscard]] bool hasValue() const noexcept { return status_ == ServiceStatus::Ok; }

    // Precondition: hasValue() is true. The pointer form is safe on failures.
    [[nodiscard]] T& value() & noexcept { return payload_.value; }
    [[nodiscard]] const T& value() const& noexcept { return payload_.value; }
    [[nodiscard]] T&& value() && noexcept { return std::move(payload_.value); }
    [[nodiscard]] T* valueOrNull() noexcept
    {
        return hasValue() ? std::addressof(payload_.value) : nullptr;
    }
    [[nodiscard]] const T* valueOrNull() const noexcept
    {
        return hasValue() ? std::addressof(payload_.value) : nullptr;
    }

private:
    struct SuccessTag {};
    union Payload {
        char empty;
        T value;
        constexpr Payload() noexcept : empty{} {}
        ~Payload() {}
    };

    explicit ServiceResult(ServiceStatus status) noexcept : status_(status) {}

    template <class Factory>
    ServiceResult(SuccessTag, Factory&& factory)
        : status_(ServiceStatus::Ok)
    {
        ::new (static_cast<void*>(std::addressof(payload_.value))) T(std::forward<Factory>(factory)());
    }

    ServiceStatus status_;
    Payload payload_;
};

template <>
class ServiceResult<void> {
public:
    [[nodiscard]] static constexpr ServiceResult success() noexcept
    {
        return ServiceResult{ServiceStatus::Ok};
    }

    [[nodiscard]] static ServiceResult failure(ServiceStatus status) noexcept
    {
        result_detail::requireFailure(status);
        return ServiceResult{status};
    }

    [[nodiscard]] constexpr ServiceStatus status() const noexcept { return status_; }
    [[nodiscard]] constexpr bool hasValue() const noexcept
    {
        return status_ == ServiceStatus::Ok;
    }

private:
    explicit constexpr ServiceResult(ServiceStatus status) noexcept : status_(status) {}
    ServiceStatus status_;
};

} // namespace telemetry

#endif
