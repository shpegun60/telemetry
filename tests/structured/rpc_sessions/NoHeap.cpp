/* The complete runtime matrix runs with all C++ allocation entry points refused. MIT. */
#include <cstdlib>
#include <new>

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

#include "Check.cpp"
