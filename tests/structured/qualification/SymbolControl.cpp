/* Real newlib symbol retention for an offline negative ELF control.
 * Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT. */
#include <cstddef>

struct _reent;
extern "C" void* _malloc_r(_reent*, std::size_t);
extern "C" int _printf_r(_reent*, const char*, ...);

// Volatile loads keep both references reachable through section GC. The
// definitions come from libc, not from synthetic functions in this fixture.
static auto volatile allocateTarget = &_malloc_r;
static auto volatile formatTarget = &_printf_r;

int main()
{
	return allocateTarget == nullptr || formatTarget == nullptr;
}
