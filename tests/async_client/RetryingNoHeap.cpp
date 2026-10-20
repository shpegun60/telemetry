/* Repeat real retry/client/Model cases with global C++ allocation trapped. MIT. */
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <new>

[[noreturn]] static void heapForbidden() noexcept
{
	std::fputs("RETRYING_HEAP_FORBIDDEN\n", stderr);
	std::_Exit(86);
}

void* operator new(std::size_t)
{
	heapForbidden();
}

void* operator new[](std::size_t)
{
	heapForbidden();
}

void* operator new(std::size_t, std::align_val_t)
{
	heapForbidden();
}

void* operator new[](std::size_t, std::align_val_t)
{
	heapForbidden();
}

void* operator new(std::size_t, const std::nothrow_t&) noexcept
{
	heapForbidden();
}

void* operator new[](std::size_t, const std::nothrow_t&) noexcept
{
	heapForbidden();
}

void* operator new(std::size_t, std::align_val_t, const std::nothrow_t&) noexcept
{
	heapForbidden();
}

void* operator new[](std::size_t, std::align_val_t, const std::nothrow_t&) noexcept
{
	heapForbidden();
}

void operator delete(void*) noexcept
{}

void operator delete[](void*) noexcept
{}

void operator delete(void*, std::size_t) noexcept
{}

void operator delete[](void*, std::size_t) noexcept
{}

void operator delete(void*, std::align_val_t) noexcept
{}

void operator delete[](void*, std::align_val_t) noexcept
{}

void operator delete(void*, std::size_t, std::align_val_t) noexcept
{}

void operator delete[](void*, std::size_t, std::align_val_t) noexcept
{}

#define RETRYING_NO_HEAP
#include "Retrying.cpp"
