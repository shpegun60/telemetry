// Independent numeric-formatting oracles and output bounds (MIT).
// Authors: Ruslan Kovtun (shpegun60), codexAi.
#include <resource/telemetry/detail/Stream.hpp>
#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string_view>

unsigned checks = 0;
#define CHECK(...)                                                                                 \
    do                                                                                             \
    {                                                                                              \
        ++checks;                                                                                  \
        if (!(__VA_ARGS__))                                                                        \
        {                                                                                          \
            std::fprintf(stderr, "line %d: %s\n", __LINE__, #__VA_ARGS__);                         \
            std::abort();                                                                          \
        }                                                                                          \
    } while (false)

void floating(double value)
{
    struct Guarded
    {
        char before = 'X';
        char text[32];
        char after = 'Y';
    } output;

    std::memset(output.text, 'Z', sizeof(output.text));
    const auto size = telemetry_resource::detail::floatingText(value, output.text);
    CHECK(output.before == 'X' && output.after == 'Y' && size <= 24);
    CHECK(output.text[size] == 'Z');
    if (!std::isfinite(value))
    {
        CHECK(size == 0);
        return;
    }
    char chars[64];
    const auto converted =
        std::to_chars(chars, chars + sizeof(chars), value, std::chars_format::general, 17);
    CHECK(converted.ec == std::errc{});
    const auto expected = std::string_view(chars, converted.ptr - chars);
    const auto actual = std::string_view(output.text, size);
    if (actual != expected)
    {
        std::fprintf(stderr, "bits=%016llx actual=%.*s expected=%.*s\n",
                     static_cast<unsigned long long>(std::bit_cast<std::uint64_t>(value)),
                     static_cast<int>(size), output.text, static_cast<int>(expected.size()), chars);
    }
    CHECK(actual == expected);
    // A second implementation also guards against sharing a rounding mistake
    // with a single oracle. This standalone process starts in the C locale.
    char printfText[64];
    const int printfSize = std::snprintf(printfText, sizeof(printfText), "%.17g", value);
    CHECK(printfSize > 0 && actual == std::string_view(printfText, printfSize));
}

template <class T>
void integer(T value)
{
    std::array<std::byte, 32> output{};
    telemetry_resource::detail::Writer writer{output, 0};
    CHECK(writer.integer(value));
    char expected[32];
    const auto converted = std::to_chars(expected, expected + sizeof(expected), value);
    CHECK(converted.ec == std::errc{});
    CHECK(std::string_view(reinterpret_cast<const char*>(output.data()), writer.used()) ==
          std::string_view(expected, converted.ptr - expected));
}

template <class T>
void integerBounds()
{
    integer(T{0});
    integer(T{1});
    integer(std::numeric_limits<T>::max());
    integer(std::numeric_limits<T>::lowest());
}

int main()
{
    for (double value :
         {0.0, -0.0, 1.0, -1.0, 0.1, 0.125, 230.0, 1e-4, 1e-5, 1e16, 1e17, 0x1p-25, 0x3p-25,
          std::numeric_limits<double>::denorm_min(), std::numeric_limits<double>::min(),
          std::numeric_limits<double>::max(), std::numeric_limits<double>::infinity(),
          -std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
    {
        floating(value);
    }
    // Visit every finite binary exponent, subnormal boundary and mantissa
    // endpoint with both signs. Include immediate decimal-boundary neighbors.
    for (std::uint64_t exponent = 0; exponent < 0x7ff; ++exponent)
    {
        for (std::uint64_t mantissa :
             {0ull, 1ull, 0x7ffffffffffffull, 0x8000000000000ull, 0xfffffffffffffull})
        {
            const auto bits = (exponent << 52) | mantissa;
            floating(std::bit_cast<double>(bits));
            floating(std::bit_cast<double>(bits | (std::uint64_t{1} << 63)));
        }
    }
    for (int exponent = -323; exponent <= 308; ++exponent)
    {
        const double value = std::pow(10.0, exponent);
        floating(value);
        floating(std::nextafter(value, 0.0));
        floating(std::nextafter(value, std::numeric_limits<double>::infinity()));
    }
    std::uint64_t random = 91;
    for (unsigned trial = 0; trial < 50000; ++trial)
    {
        random = random * 6364136223846793005ull + 1442695040888963407ull;
        floating(std::bit_cast<double>(random));
        if (trial < 10000)
        {
            floating(static_cast<double>(std::bit_cast<float>(static_cast<std::uint32_t>(random))));
        }
    }
    integerBounds<std::uint8_t>();
    integerBounds<std::uint16_t>();
    integerBounds<std::uint32_t>();
    integerBounds<std::uint64_t>();
    integerBounds<std::int8_t>();
    integerBounds<std::int16_t>();
    integerBounds<std::int32_t>();
    integerBounds<std::int64_t>();
    std::printf("Resource numeric formatting: %u checks passed\n", checks);
}
