/* Entry point and no-C++-allocation control. Authors: Ruslan Kovtun
 * (shpegun60), codexAi. SPDX-License-Identifier: MIT. */
#include "Fixture.hpp"
#include <cstdlib>
#include <new>
#ifndef QUALIFICATION_ARM
#include <cstdio>
#endif

// Allocation through any C++ new form terminates, including in helpers.
void* operator new(std::size_t)
{
	std::abort();
}

void* operator new[](std::size_t)
{
	std::abort();
}

void* operator new(std::size_t, std::align_val_t)
{
	std::abort();
}

void* operator new[](std::size_t, std::align_val_t)
{
	std::abort();
}

void operator delete(void*) noexcept
{
	std::abort();
}

void operator delete[](void*) noexcept
{
	std::abort();
}

void operator delete(void*, std::size_t) noexcept
{
	std::abort();
}

void operator delete[](void*, std::size_t) noexcept
{
	std::abort();
}

void operator delete(void*, std::align_val_t) noexcept
{
	std::abort();
}

void operator delete[](void*, std::align_val_t) noexcept
{
	std::abort();
}

void operator delete(void*, std::size_t, std::align_val_t) noexcept
{
	std::abort();
}

void operator delete[](void*, std::size_t, std::align_val_t) noexcept
{
	std::abort();
}

int main()
{
	using namespace qualification;
	// The explicit module check belongs at startup, never in a hot handler.
	ts::requireStructuredAbi();
	consumer_typed();
	consumer_encoded();
#ifndef QUALIFICATION_ARM
	std::printf("{\"checks\":%u,\"failures\":%u}\n", checks, failures);
#endif
	return failures == 0 ? 0 : 1;
}
