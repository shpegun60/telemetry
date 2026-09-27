/*
 * @file ResultProbe.cpp
 * @brief ServiceResult state, payload lifetime and copy/move transitions.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */

#include <telemetry_structured/result/ServiceResult.hpp>

#include <cstdlib>
#include <type_traits>
#include <utility>

using telemetry::structured::ServiceResult;
using telemetry::structured::ServiceStatus;

namespace {

int live = 0;

struct Counted {
    int value;
    explicit Counted(int input) noexcept : value(input) { ++live; }
    Counted(const Counted& other) noexcept : value(other.value) { ++live; }
    Counted(Counted&& other) noexcept : value(other.value) { ++live; }
    Counted& operator=(const Counted& other) noexcept
    {
        value = other.value;
        return *this;
    }
    Counted& operator=(Counted&& other) noexcept
    {
        value = other.value;
        return *this;
    }
    ~Counted() { --live; }
};

void require(bool condition)
{
    if (!condition) std::abort();
}

static_assert(!std::is_aggregate_v<ServiceResult<int>>);

} // namespace

int main()
{
    {
        auto a = ServiceResult<Counted>::successFrom([] { return Counted{7}; });
        require(a.status() == ServiceStatus::Ok && a.hasValue());
        require(a.value().value == 7 && a.valueOrNull() == &a.value());
        require(live == 1);

        auto b = ServiceResult<Counted>::failure(ServiceStatus::Busy);
        require(!b.hasValue() && b.valueOrNull() == nullptr && live == 1);
        auto c = a;
        require(c.hasValue() && c.value().value == 7 && live == 2);
        auto d = std::move(c);
        require(d.hasValue() && d.value().value == 7 && live == 3);

        b = a;  // failure -> success
        require(b.hasValue() && live == 4);
        a = ServiceResult<Counted>::failure(ServiceStatus::Failed); // success -> failure
        require(!a.hasValue() && live == 3);
        c = std::move(a); // success -> failure by move assignment
        require(!c.hasValue() && live == 2);
        a = std::move(d); // failure -> success by move assignment
        require(a.hasValue() && live == 3);
        auto& alias = a;
        a = alias; // self-assignment preserves the active member
        require(a.hasValue() && live == 3);
    }
    require(live == 0);

    auto small = ServiceResult<int>::success(42);
    require(small.hasValue() && small.value() == 42);
    auto noPayload = ServiceResult<void>::success();
    require(noPayload.hasValue() && noPayload.status() == ServiceStatus::Ok);
    auto failed = ServiceResult<void>::failure(ServiceStatus::Unavailable);
    require(!failed.hasValue() && failed.status() == ServiceStatus::Unavailable);
}
