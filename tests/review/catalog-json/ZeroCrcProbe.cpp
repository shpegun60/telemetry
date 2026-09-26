// Review probe (catalog-json): schemaCrc() returns 0 as its "null metadata"
// error value, but 0 is not reserved: FNV-1a is invertible, so a valid schema
// whose fingerprint is 0 can be constructed. Meet-in-the-middle over a
// six-character printable catalog name; then the library confirms it.
#include "serialization/TelemetryJson.h"
#include <cstdio>
#include <cstring>
#include <unordered_map>

using namespace telemetry;
namespace {
constexpr std::uint32_t prime = 16777619u;
std::uint32_t inverse(std::uint32_t a)
{
    std::uint32_t x = 1; // Newton iteration for the 2-adic inverse of an odd number.
    for (int i = 0; i < 6; ++i) x *= 2u - a * x;
    return x;
}
std::uint32_t step(std::uint32_t h, unsigned char b) { return (h ^ b) * prime; }
}

int main()
{
    const std::uint32_t pinv = inverse(prime);
    // The empty schema's fingerprint is the constant header prefix (110cc495).
    const std::uint32_t header = schemaCrc(CatalogIndex{});
    const std::uint32_t start = step(step(step(header, 'C'), 0), 0); // catalog 0 index bytes
    // Required state after the name bytes: name NUL, then 'E', must give 0.
    const std::uint32_t afterNul = 'E';            // (afterNul ^ 'E') * P == 0
    const std::uint32_t target = afterNul * pinv;  // (target ^ 0) * P == afterNul
    // Backward three bytes from target.
    std::unordered_map<std::uint32_t, unsigned> backward;
    backward.reserve(900000);
    for (unsigned a = 0x21; a < 0x7f; ++a)
        for (unsigned b = 0x21; b < 0x7f; ++b)
            for (unsigned c = 0x21; c < 0x7f; ++c) {
                // name = ... x y z with step(step(step(s,x),y),z) == target, here z=c, y=b, x=a
                const std::uint32_t beforeC = (target * pinv) ^ c;
                const std::uint32_t beforeB = (beforeC * pinv) ^ b;
                const std::uint32_t beforeA = (beforeB * pinv) ^ a;
                backward.emplace(beforeA, (a << 16) | (b << 8) | c);
            }
    for (unsigned a = 0x21; a < 0x7f; ++a)
        for (unsigned b = 0x21; b < 0x7f; ++b)
            for (unsigned c = 0x21; c < 0x7f; ++c) {
                const std::uint32_t state = step(step(step(start, a), b), c);
                const auto found = backward.find(state);
                if (found == backward.end()) continue;
                char name[8] = {static_cast<char>(a), static_cast<char>(b), static_cast<char>(c),
                                static_cast<char>(found->second >> 16), static_cast<char>(found->second >> 8),
                                static_cast<char>(found->second), 0, 0};
                if (std::strchr(name, '"') || std::strchr(name, '\\')) continue;
                const Catalog catalogs[] = {{name, nullptr, 0}};
                char text[256];
                const auto crc = schemaCrc(catalogs, 1);
                const auto length = writeSchema(catalogs, 1, text, sizeof text);
                std::printf("catalog name \"%s\": schemaCrc = %08lx, writeSchema length %zu\n%s\n",
                            name, static_cast<unsigned long>(crc), length, text);
                return crc == 0 && length != 0 ? 0 : 1;
            }
    std::printf("no match\n");
    return 2;
}
