/*
 * @file TypesNegative.cpp
 * @brief Compile-time rejection cases for unsupported wire shapes.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */

#include <telemetry_structured/type/Traits.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

struct Empty {};
struct Reading { float value; std::uint16_t status; };

#if CASE == 1
struct Bad { int* value; };
#elif CASE == 2
struct Bad { const int value; };
#elif CASE == 3
struct Bad { int values[3]; };
#elif CASE == 4
struct Bad { int& value; };
#elif CASE == 5
struct Bad { unsigned value : 1; };
#elif CASE == 6
struct Bad { std::string value; };
#elif CASE == 7
struct Base { int x; };
struct Bad : Base { int y; };
#elif CASE == 8
union Bad { int x; float y; };
#elif CASE == 9
using Bad = std::array<Reading, 200000>;
#elif CASE == 10
using Bad = std::array<std::array<Empty, 65536>, 65536>;
#elif CASE == 11
struct Bad { volatile int value; };
#elif CASE == 12
struct Bad { std::span<int> values; };
#elif CASE == 13
struct Bad { std::optional<int> value; };
#elif CASE == 14
struct Bad { int value; Bad(int x) : value(x) {} };
#elif CASE == 15
struct Bad { private: int value; };
#elif CASE == 16
struct Bad { int ΔLimit; };
#elif CASE == 17
struct __attribute__((packed)) Bad { std::uint8_t first; std::uint32_t second; };
#elif CASE == 18
using Bad = std::array<std::array<std::uint64_t, 65536>, 3>;
#elif CASE == 19
using Bad = std::array<const std::uint8_t, 2>;
#elif CASE == 20
template <std::size_t N> struct Nest { std::array<Nest<N - 1>, 1> next; };
template <> struct Nest<0> { std::uint8_t value; };
using Bad = Nest<33>;
#elif CASE == 21
struct Bad { char value; };
#elif CASE == 22
using Bad = long double;
#elif CASE == 23
struct alignas(4) Bad {
    std::uint8_t first;
    std::uint32_t second __attribute__((packed));
};
#elif CASE == 24
struct EmptyBase {};
struct Bad : EmptyBase { std::uint32_t value; };
#endif
std::uint32_t probe() { return telemetry::structured::wireSize<Bad>; }
