/* Construction and reads with dynamic allocation forbidden. MIT. */
#include <cstdlib>
#include <new>
void* operator new(std::size_t) { std::abort(); }
void* operator new[](std::size_t) { std::abort(); }
void* operator new(std::size_t, std::align_val_t) { std::abort(); }
void* operator new[](std::size_t, std::align_val_t) { std::abort(); }
void operator delete(void*) noexcept { std::abort(); }
void operator delete[](void*) noexcept { std::abort(); }
void operator delete(void*, std::size_t) noexcept { std::abort(); }
void operator delete[](void*, std::size_t) noexcept { std::abort(); }
void operator delete(void*, std::align_val_t) noexcept { std::abort(); }
void operator delete[](void*, std::align_val_t) noexcept { std::abort(); }
void operator delete(void*, std::size_t, std::align_val_t) noexcept { std::abort(); }
void operator delete[](void*, std::size_t, std::align_val_t) noexcept { std::abort(); }

#include "Fixture.hpp"
int main()
{
    const resource::structured::Descriptor descriptor{fixture::bigModel};
    const resource::structured::ValuesFile values{descriptor, fixture::bigWorkspace};
    std::array<std::byte, fixture::bigValues.size()> buffer;
    const auto result = values.read(0, buffer);
    return result.eof && result.written == buffer.size() && fixture::bigWorkspace.used() == 0 ? 0 : 1;
}
