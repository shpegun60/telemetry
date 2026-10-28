/* The SDK Reset_Handler initializes .data/.bss and invokes __libc_init_array.
 * There is no hosted process, I/O syscall, heap or application exit. MIT. */
#include "main.h"
extern "C" {
void _init()
{}

void _fini()
{}

[[noreturn]] void abort() noexcept
{
	Error_Handler();
	__builtin_unreachable();
}

[[noreturn]] void _exit(int)
{
	Error_Handler();
	__builtin_unreachable();
}

void __cxa_pure_virtual()
{
	Error_Handler();
}
}
