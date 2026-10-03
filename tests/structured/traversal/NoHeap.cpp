/* Metadata visitors and checked native access require no allocation. MIT. */
#include "Fixture.hpp"
#include <cassert>
#include <cstdlib>
#include <new>
void* operator new(std::size_t) { std::abort(); }
void* operator new[](std::size_t) { std::abort(); }
void operator delete(void*) noexcept { std::abort(); }
void operator delete[](void*) noexcept { std::abort(); }
void operator delete(void*, std::size_t) noexcept { std::abort(); }
void operator delete[](void*, std::size_t) noexcept { std::abort(); }
using namespace fixture;
int main()
{
    unsigned total = 0;
    auto visitor = [&](const auto& endpoint) {
        assert(endpoint.name() != nullptr);
        ++total;
    };
    localFields.forEach(visitor);
    for (unsigned i = 0; i != localFields.size(); ++i) assert(localFields.visit(i, visitor));
    for (unsigned i = 0; i != localCommands.size(); ++i) assert(localCommands.visit(i, visitor));
    for (unsigned i = 0; i != localServices.size(); ++i) assert(localServices.visit(i, visitor));
    assert(total == 27 && device.reads == 0 && device.services == 0);
    assert(fields.readAs<double>(1) == 65535.0);
    assert(fields.writeAs(1, 123.75) == W::Applied);
    // The caller explicitly requests and owns Big. Selection itself is small.
    auto big = fields.readAs<Big>(8);
    assert(big && big->words[1] == 2);
}
