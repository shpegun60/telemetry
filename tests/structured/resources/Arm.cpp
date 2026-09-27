/* Actual compiled provider boundary, constant storage and stack roots. MIT. */
#include "Fixture.hpp"
using namespace fixture;
__attribute__((noinline)) resource::ReadResult values_read(
    std::uint64_t cursor, std::byte* output, std::uint32_t size) noexcept
{ return values.read(cursor, {output, size}); }
__attribute__((noinline)) resource::ReadResult big_read(
    std::uint64_t cursor, std::byte* output, std::uint32_t size) noexcept
{ return bigValues.read(cursor, {output, size}); }
__attribute__((noinline)) resource::ReadResult packed_read(
    std::uint64_t cursor, std::byte* output, std::uint32_t size) noexcept
{ return packedFile.read(cursor, {output, size}); }
extern "C" std::uint32_t resource_roots(std::byte* output, std::uint32_t size, std::uint64_t cursor) noexcept
{
    return values_read(cursor, output, size).written + big_read(cursor, output, size).written +
           packed_read(cursor, output, size).written;
}
#ifdef ABI_MAIN
int main() { std::array<std::byte, 73> out; return values.read(0, out).eof ? 0 : 1; }
#endif
