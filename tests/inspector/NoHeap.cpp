/*
 * @file NoHeap.cpp
 * @brief Execute real InspectorModel reads with C++ allocations trapped.
 * SPDX-License-Identifier: MIT
 */
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <new>

[[noreturn]] static void heapDetected() noexcept
{
	std::fputs("inspector heap allocation refused\n", stderr);
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

#define main inspectorHostMain
#include "Host.cpp"
#undef main

int main()
{
#ifdef INSPECTOR_HEAP_CONTROL
	void* volatile allocation = ::operator new(1);
	(void)allocation;
	return 1;
#else
	return inspectorHostMain();
#endif
}
