// Review probe (numeric-core): are over-aligned Field objects on the stack
// actually aligned to cacheLineBytes? README: "Ordinary Field[], std::array
// <Field, N> and conforming C++17 allocation provide the required alignment."
#include "field/TelemetryField.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

using namespace telemetry;

namespace {
int misaligned = 0;
void report(const char* what, const void* address)
{
    const auto value = reinterpret_cast<std::uintptr_t>(address);
    const bool ok = value % alignof(Field) == 0;
    if (!ok) ++misaligned;
    std::printf("%s  %-28s %p mod %zu = %zu\n", ok ? "PASS" : "FAIL", what, address,
                alignof(Field), static_cast<std::size_t>(value % alignof(Field)));
}

__attribute__((noinline)) void leaf(int depth)
{
    // Different frame depths change the incoming stack offset.
    char pad[1 + (sizeof(void*) * 3)];
    pad[0] = static_cast<char>(depth);
    const Field rows[2] = {{"a"}, {"b"}};
    report("local Field[2]", rows);
    std::array<Field, 1> array{{Field{"c"}}};
    report("local std::array<Field,1>", array.data());
    asm volatile("" : : "r"(pad) : "memory");
}

template <int N>
__attribute__((noinline)) void nest()
{
    volatile char spill[N * 8 + 1] = {};
    (void) spill;
    leaf(N);
}
} // namespace

int main()
{
    std::printf("cacheLineBytes=%zu alignof(Field)=%zu sizeof(Field)=%zu\n",
                cacheLineBytes, alignof(Field), sizeof(Field));
    nest<0>(); nest<1>(); nest<2>(); nest<3>(); nest<5>();
    auto heap = std::make_unique<Field>("heap");
    report("new Field", heap.get());
    std::vector<Field> vector;
    vector.reserve(3);
    vector.emplace_back("v");
    report("std::vector<Field>", vector.data());
    std::printf("%d misaligned\n", misaligned);
    return misaligned == 0 ? 0 : 1;
}
