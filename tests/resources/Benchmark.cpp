// Host A/B timing only; these timings are not Cortex-M cycle measurements (MIT).
// Authors: Ruslan Kovtun (shpegun60), codexAi.
#include <resource/telemetry/detail/Stream.hpp>
#include <array>
#include <bit>
#include <chrono>
#include <cstdio>
#include <limits>
#include <string>

template <class Work>
void measure(const char* name, std::size_t count, Work work)
{
    auto fastest = std::chrono::nanoseconds::max();
    std::uint64_t checksum = 0;
    for (unsigned repeat = 0; repeat < 5; ++repeat)
    {
        const auto start = std::chrono::steady_clock::now();
        for (std::size_t i = 0; i < count; ++i)
        {
            checksum += work(i);
        }
        const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - start);
        fastest = std::min(fastest, elapsed);
    }
    std::printf("%s,%.2f,%llu\n", name, double(fastest.count()) / count,
                static_cast<unsigned long long>(checksum));
}

int main()
{
    using namespace telemetry_resource::detail;
    const double common[] = {0.0, -0.0, 230.0, 0.1, 0.125, -12.75, 1e-4, 300.01};
    std::array<double, 256> varied{};
    std::uint64_t random = 91;
    for (auto& number : varied)
    {
        random = random * 6364136223846793005ull + 1;
        number = std::bit_cast<double>(random & UINT64_C(0xffefffffffffffff));
    }
    auto format = [](double number)
    {
        char output[32];
        const auto size = floatingText(number, output);
        return size + static_cast<unsigned char>(output[size - 1]);
    };
    measure("float_common_ns", 200000,
            [&](std::size_t i)
            {
                return format(common[i % std::size(common)]);
            });
    measure("float_varied_ns", 10000,
            [&](std::size_t i)
            {
                return format(varied[i % varied.size()]);
            });
    std::string label(8192, 'a');
    for (std::uint32_t offset : {0u, 4096u})
    {
        measure(offset == 0 ? "string_start_ns" : "string_resume_ns", 20000,
                [&](std::size_t)
                {
                    std::array<std::byte, 64> buffer{};
                    Writer out{buffer, offset};
                    (void)out.requiredString(label.c_str());
                    return out.used() + std::to_integer<unsigned>(buffer.back());
                });
    }
}
