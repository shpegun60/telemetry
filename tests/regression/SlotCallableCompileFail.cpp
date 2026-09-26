// Reject calls that copy a mutable reference or convert native slot types.
// Authors: Ruslan Kovtun (shpegun60), codexAi. License: MIT.
#include "Telemetry.h"
using namespace telemetry;

#ifndef TELEMETRY_SLOT_CALLABLE_FAIL_CASE
#define TELEMETRY_SLOT_CALLABLE_FAIL_CASE 0
#endif

#if TELEMETRY_SLOT_CALLABLE_FAIL_CASE == 1
void bad() { DelegateSlot<void(int&) noexcept> slot; slot.bind([](auto x) noexcept { (void)x; }); }
#elif TELEMETRY_SLOT_CALLABLE_FAIL_CASE == 2
void bad() { DelegateRefSlot<void(int&) noexcept> slot; auto f = [](auto x) noexcept { (void)x; }; slot.bind(f); }
#elif TELEMETRY_SLOT_CALLABLE_FAIL_CASE == 3
void bad() { DelegateSlot<void(int&) noexcept> slot; slot.bind([](auto... xs) noexcept { ((void)xs, ...); }); }
#elif TELEMETRY_SLOT_CALLABLE_FAIL_CASE == 4
void bad() { DelegateSlot<void(int&, int&) noexcept> slot; slot.bind([](auto x, auto& y) noexcept { y = x; }); }
#elif TELEMETRY_SLOT_CALLABLE_FAIL_CASE == 5
void bad() { DelegateSlot<void(int&, int&) noexcept> slot; slot.bind([](auto& x, auto y) noexcept { x = y; }); }
#elif TELEMETRY_SLOT_CALLABLE_FAIL_CASE == 6
void bad() { DelegateSlot<void(int&&) noexcept> slot; slot.bind([](auto x) noexcept { (void)x; }); }
#elif TELEMETRY_SLOT_CALLABLE_FAIL_CASE == 7
struct Target {
    template <class T> void operator()(T&) const noexcept {}
    void operator()(int) const noexcept {}
};
void bad() { DelegateSlot<void(int&) noexcept> slot; slot.bind(Target{}); }
#elif TELEMETRY_SLOT_CALLABLE_FAIL_CASE == 8
struct Target {
    template <class T> void operator()(T&) {}
    void operator()(int) noexcept {}
};
void bad() { DelegateSlot<void(int&) noexcept> slot; slot.bind(Target{}); }
#elif TELEMETRY_SLOT_CALLABLE_FAIL_CASE == 9
struct Target {
    void operator()(int&) const noexcept {}
    void operator()(int) const noexcept {}
};
void bad() { DelegateSlot<void(int&) noexcept> slot; slot.bind(Target{}); }
#elif TELEMETRY_SLOT_CALLABLE_FAIL_CASE == 10
struct Target {
    void operator()(int&) const noexcept {}
    void operator()(int) noexcept {}
};
void bad() { DelegateSlot<void(int&) noexcept> slot; slot.bind(Target{}); }
#elif TELEMETRY_SLOT_CALLABLE_FAIL_CASE == 11
void bad() { DelegateSlot<int(int) noexcept> slot; slot.bind([](auto x) noexcept { return double(x); }); }
#elif TELEMETRY_SLOT_CALLABLE_FAIL_CASE == 12
void bad() { DelegateSlot<void(int) noexcept> slot; slot.bind([](double x) noexcept { (void)x; }); }
#elif TELEMETRY_SLOT_CALLABLE_FAIL_CASE == 13
void bad() { DelegateSlot<void(int&, float) noexcept> slot; slot.bind([](auto& a, auto& b) noexcept { a = int(b); }); }
#elif TELEMETRY_SLOT_CALLABLE_FAIL_CASE == 14
struct Target {
    template <class T, std::enable_if_t<std::is_integral_v<T>, int> = 0>
    void operator()(T) const noexcept {}
};
void bad() { DelegateSlot<void(int&) noexcept> slot; slot.bind(Target{}); }
#elif TELEMETRY_SLOT_CALLABLE_FAIL_CASE == 15
struct Target {
    template <class T> void operator()(T x, T y, int& result) const noexcept { result = x + y; }
};
void bad() { DelegateSlot<void(int&, int&, int&) noexcept> slot; slot.bind(Target{}); }
#elif TELEMETRY_SLOT_CALLABLE_FAIL_CASE == 0
void good() { DelegateSlot<void(int&) noexcept> slot; slot.bind([](auto&& x) noexcept { ++x; }); }
#else
#error Select TELEMETRY_SLOT_CALLABLE_FAIL_CASE from 0 through 15
#endif
int main() {}
