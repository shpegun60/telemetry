/* Heap-free complete handshake and small/large request dispatch. MIT. */
#include "Fixture.hpp"
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
using namespace fixture;
int main()
{
    rs::Binding peer; ready(peer);
    std::array<std::byte, model.maxScratch()> memory{};
    ts::Workspace workspace{memory};
    std::array<std::byte, 24 + ts::wireSize<Big>> output{};
    for (const auto operation : {Op::FieldWrite, Op::Command, Op::Service}) {
        const auto request = packet(operation, 0, Config{99, true});
        assert(rs::Exchange::process(peer, request, output, workspace).dispatch == D::Ok);
    }
    const auto request = packet(Op::Service, 3, Config{111, false});
    assert(rs::Exchange::process(peer, request, output, workspace).written == output.size());
    assert(get32(output, 24) == 111 && workspace.used() == 0);
    peer.reset();
    assert(rs::Exchange::process(peer, request, output, workspace).dispatch == D::NotReady);
}
