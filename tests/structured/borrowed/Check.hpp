/* Counted contracts and host allocation observation. Authors: Ruslan Kovtun
 * (shpegun60), codexAi. SPDX-License-Identifier: MIT. */
#pragma once
#include <cstddef>
#include <cstdlib>
#ifndef BORROWED_ARM
#include <cstdio>
#include <new>
#endif

namespace borrowed_test {
inline unsigned checks = 0, failures = 0, allocations = 0;
inline bool watching = false;
inline void check(bool condition) noexcept { ++checks; failures += !condition; }
inline void start() noexcept { watching = true; }
inline int finish() noexcept
{
    check(allocations == 0);
    watching = false;
#ifndef BORROWED_ARM
    std::printf("{\"checks\":%u,\"failures\":%u}\n", checks, failures);
#endif
    return failures == 0 ? 0 : 1;
}
}

// Observe allocations during the actual operations, not printf or host startup.
// ARM roots are linked separately and inspected for live allocation symbols.
#ifndef BORROWED_ARM
inline void* borrowed_allocate(std::size_t bytes)
{
    if (borrowed_test::watching) ++borrowed_test::allocations;
    if (auto* result = std::malloc(bytes == 0 ? 1 : bytes)) return result;
    std::abort();
}
void* operator new(std::size_t bytes) { return borrowed_allocate(bytes); }
void* operator new[](std::size_t bytes) { return borrowed_allocate(bytes); }
void operator delete(void* value) noexcept { std::free(value); }
void operator delete[](void* value) noexcept { std::free(value); }
void operator delete(void* value, std::size_t) noexcept { std::free(value); }
void operator delete[](void* value, std::size_t) noexcept { std::free(value); }
#endif
