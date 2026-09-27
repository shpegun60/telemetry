/* Ordinary data dispatch is independent of descriptor hashing/Bind. MIT. */
#include "Fixture.hpp"
using namespace fixture;
#ifdef ARM_PROBE
extern "C" [[noreturn]] void abort() { __builtin_trap(); }
#endif

extern "C" std::uint32_t exchange_packet(const rs::Binding& peer, const std::byte* input,
    std::uint32_t count, std::byte* output, std::uint32_t capacity, ts::Workspace& workspace) noexcept
{
    const auto result = rs::Exchange::process(peer, {input, count}, {output, capacity}, workspace);
    return result.written + static_cast<std::uint32_t>(result.dispatch);
}

extern "C" std::uint32_t bind_packet(rs::Binding& peer, const std::byte* input,
    std::uint32_t count, std::byte* output, std::uint32_t capacity) noexcept
{
    const auto result = rs::Bind::process(peer, view, descriptor.fingerprint(), {input, count}, {output, capacity});
    return result.written + static_cast<std::uint32_t>(result.dispatch);
}

// Bounded static transport buffers; large objects are not placed on the stack.
static rs::Binding peer;
static std::array<std::byte, model.maxScratch()> storage;
static ts::Workspace workspace{storage};
static std::array<std::byte, 24 + ts::wireSize<Big>> input, output;
extern "C" std::uint32_t exchange_roots(unsigned operation) noexcept
{
    if (operation == 0) return bind_packet(peer, input.data(), 16, output.data(), output.size());
    return exchange_packet(peer, input.data(), input.size(), output.data(), output.size(), workspace);
}

#ifdef ABI_MAIN
static volatile unsigned selectedOperation = 1;
int main() { return static_cast<int>(exchange_roots(selectedOperation)); }
#endif
