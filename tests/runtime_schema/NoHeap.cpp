/*
 * @file NoHeap.cpp
 * @brief Execute runtime reads with replaceable C++ allocation functions trapped.
 * SPDX-License-Identifier: MIT
 */
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <new>

[[noreturn]] static void heapDetected() noexcept
{
	std::fputs("runtime heap allocation refused\n", stderr);
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

#define main runtimeCheckMain
#include "Check.cpp"
#undef main

int main(int argc, char** argv)
{
#ifdef RUNTIME_HEAP_CONTROL
	(void)argc;
	(void)argv;
	void* volatile allocation = ::operator new(1);
	(void)allocation;
	return 1;
#else
	return runtimeCheckMain(argc, argv);
#endif
}
