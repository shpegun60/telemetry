/* Exercise the real writer while C++ allocation/deallocation is refused.
 * Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
 */
#include <cstdlib>
#include <new>

[[noreturn]] static void heapDetected() noexcept
{
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

#define main wireWriterMain
#include "Host.cpp"
#undef main

int main(int argc, char** argv)
{
	if (argc == 1)
		return wireWriterMain();
	void* volatile allocation = nullptr;
	switch (std::atoi(argv[1])) {
		case 1:
			allocation = ::operator new(1);
			break;
		case 2:
			allocation = ::operator new[](1);
			break;
		case 3:
			allocation = ::operator new(1, std::align_val_t{64});
			break;
		case 4:
			allocation = ::operator new[](1, std::align_val_t{64});
			break;
		case 5:
			allocation = ::operator new(1, std::nothrow);
			break;
		case 6:
			allocation = ::operator new[](1, std::nothrow);
			break;
		default:
			return 2;
	}
	(void)allocation;
	return 1;
}
