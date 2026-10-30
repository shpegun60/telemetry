/*
 * @file NoHeap.cpp
 * @brief Execute real Workbench read/edit/write/refresh with C++ allocations trapped.
 * SPDX-License-Identifier: MIT
 */
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <new>

[[noreturn]] static void heapDetected() noexcept
{
	std::fputs("workbench heap allocation refused\n", stderr);
	std::_Exit(86);
}

void* operator new(std::size_t)
{
	heapDetected();
}

void* operator new[](std::size_t)
{
	heapDetected();
}

void* operator new(std::size_t, std::align_val_t)
{
	heapDetected();
}

void* operator new[](std::size_t, std::align_val_t)
{
	heapDetected();
}

void* operator new(std::size_t, const std::nothrow_t&) noexcept
{
	heapDetected();
}

void* operator new[](std::size_t, const std::nothrow_t&) noexcept
{
	heapDetected();
}

void* operator new(std::size_t, std::align_val_t, const std::nothrow_t&) noexcept
{
	heapDetected();
}

void* operator new[](std::size_t, std::align_val_t, const std::nothrow_t&) noexcept
{
	heapDetected();
}

void operator delete(void*) noexcept
{
	heapDetected();
}

void operator delete[](void*) noexcept
{
	heapDetected();
}

void operator delete(void*, std::size_t) noexcept
{
	heapDetected();
}

void operator delete[](void*, std::size_t) noexcept
{
	heapDetected();
}

void operator delete(void*, std::align_val_t) noexcept
{
	heapDetected();
}

void operator delete[](void*, std::align_val_t) noexcept
{
	heapDetected();
}

void operator delete(void*, std::size_t, std::align_val_t) noexcept
{
	heapDetected();
}

void operator delete[](void*, std::size_t, std::align_val_t) noexcept
{
	heapDetected();
}

void operator delete(void*, const std::nothrow_t&) noexcept
{
	heapDetected();
}

void operator delete[](void*, const std::nothrow_t&) noexcept
{
	heapDetected();
}

void operator delete(void*, std::align_val_t, const std::nothrow_t&) noexcept
{
	heapDetected();
}

void operator delete[](void*, std::align_val_t, const std::nothrow_t&) noexcept
{
	heapDetected();
}

#define main workbenchHostMain
#include "Host.cpp"
#undef main

int main()
{
#ifdef WORKBENCH_HEAP_CONTROL
	void* volatile allocation = ::operator new(1);
	(void)allocation;
	return 1;
#else
	return workbenchHostMain();
#endif
}
