/*
 * @file NoHeap.cpp
 * @brief Run the full client matrix with C++ allocation entry points refused.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
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
