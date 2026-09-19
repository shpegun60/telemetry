// Author: Ruslan Kovtun (shpegun60), codexAi. License: MIT.
// Observed stack writes, including newlib-nano; not a worst-case bound.
#include "serialization/TelemetryJson.h"
#include "field/TelemetryEnum.h"
#include "usart.h"
#include "uart_bench.h"
#include <cstdio>
#include <cstring>
#include <limits>

BenchCounter g_bench_usart_irq{}, g_bench_rx_dma_irq{}, g_bench_tx_dma_irq{};
extern "C" std::size_t telemetry_call_on_psp(std::size_t (*)() noexcept, void*) noexcept;

namespace {
using namespace telemetry;
constexpr unsigned stackBytes = 16384, guardBytes = 256, repetitions = 3;
alignas(32) __attribute__((section(".json_probe_stack")))
volatile unsigned char probeStack[guardBytes + stackBytes];
char output[4096];  // Deliberately outside the measured stack.
Scalar source;
unsigned activeCase;
bool ready;
__attribute__((noinline)) Scalar readSource() noexcept { return source; }

enum class Mode : std::uint16_t { Off, Auto, Manual };
constexpr Field nativeFields[] = {
    {0, "f32", "V", ScalarType::F32}, {1, "f64", "", ScalarType::F64},
    {2, "u64", "", ScalarType::U64}, {3, "bool", "", ScalarType::Bool},
};
constexpr Field customFields[] = {
    {0, "f32\"\\\n", "V", numericType<float>(230.125f, -999.5f, 1234.75f)},
    {1, "f64", "", numericType<double>(1.2345678901234567, -1.7e308, 1.7e308)},
    {2, "mode", "", enumType<Mode>()},
};
constexpr Field f32Fields[] = {{0, "number", "", ScalarType::F32, &readSource}};
constexpr Field f64Fields[] = {{0, "number", "", ScalarType::F64, &readSource}};
constexpr Field integerFields[] = {
    {0, "u64", "", ScalarType::U64, []() noexcept { return UINT64_MAX; }},
    {1, "s64", "", ScalarType::S64, []() noexcept { return INT64_MIN; }},
    {2, "u32", "", ScalarType::U32, []() noexcept { return UINT32_MAX; }},
    {3, "s32", "", ScalarType::S32, []() noexcept { return INT32_MIN; }},
    {4, "bool", "", ScalarType::Bool, []() noexcept { return true; }},
};
constexpr Catalog nativeCatalog[] = {{0, "v", nativeFields}};
constexpr Catalog customCatalog[] = {{0, "v", customFields}};
constexpr Catalog f32Catalog[] = {{0, "v", f32Fields}};
constexpr Catalog f64Catalog[] = {{0, "v", f64Fields}};
constexpr Catalog integerCatalog[] = {{0, "v", integerFields}};
constexpr CatalogIndex nativeIndex{nativeCatalog}, customIndex{customCatalog},
    f32Index{f32Catalog}, f64Index{f64Catalog}, integerIndex{integerCatalog};

template<class T> constexpr T numbers[] = {
    T{0}, -T{0}, T{1}, T{-1}, std::numeric_limits<T>::denorm_min(),
    -std::numeric_limits<T>::denorm_min(), std::numeric_limits<T>::min(),
    -std::numeric_limits<T>::min(), std::numeric_limits<T>::max(),
    std::numeric_limits<T>::lowest(), T(1.2345678901234567), T(0.00001),
    T(0.0001), T(1e20), T(1e-20), T(9999999.5), T(-12.7), T(230.125),
    std::numeric_limits<T>::infinity(), -std::numeric_limits<T>::infinity(),
    std::numeric_limits<T>::quiet_NaN(),
};
constexpr unsigned numberCount = sizeof(numbers<float>)/sizeof(float);

__attribute__((noinline)) std::size_t stackControl() noexcept
{
    volatile unsigned char control[512];
    for (unsigned i = 0; i < sizeof(control); ++i) control[i] = static_cast<unsigned char>(i);
    return control[511] == 255 ? 0 : 1;
}

__attribute__((noinline)) std::size_t exercise() noexcept
{
    switch (activeCase) {
    case 0: return stackControl();
    case 1: return writeSchema(nativeIndex, output, sizeof(output));
    case 2: return writeSchema(customIndex, output, sizeof(output));
    case 3: return writeValues(f32Index, output, sizeof(output));
    case 4: return writeValues(f64Index, output, sizeof(output));
    case 5: return writeValues(integerIndex, output, sizeof(output));
    case 6: return writeSchema(customIndex, output, 128);
    case 7: return writeValues(f64Index, output, 8);
    default: return writeSchema(customIndex, nullptr, 0);
    }
}

void line(const char* text)
{
    (void)HAL_UART_Transmit(&huart3, reinterpret_cast<const std::uint8_t*>(text),
                           static_cast<std::uint16_t>(std::strlen(text)), 5000);
}

std::uint32_t checksum(std::size_t length)
{
    std::uint32_t hash = 2166136261u;
    for (std::size_t i = 0; i < length; ++i) hash = (hash ^ static_cast<unsigned char>(output[i])) * 16777619u;
    return hash;
}
} // namespace

extern "C" void bench_init()
{
    ready = SystemCoreClock == 600000000u && __get_IPSR() == 0 && (__get_CONTROL() & 3u) == 0;
}

extern "C" void bench_loop()
{
    std::uint8_t command = 0;
    if (HAL_UART_Receive(&huart3, &command, 1, 100) != HAL_OK || command != 'R') return;
    if (!ready) { line("STACK FAIL setup\r\n"); return; }
    char report[192];
    std::snprintf(report, sizeof(report), "STACK READY 1 %u %lu %u %u %u %u\r\n",
        LAYOUT_OPT, static_cast<unsigned long>(SystemCoreClock), stackBytes, guardBytes, numberCount, repetitions);
    line(report);
    for (activeCase = 0; activeCase < 9; ++activeCase) {
        const unsigned samples = activeCase == 3 || activeCase == 4 ? numberCount : 1;
        for (unsigned sample = 0; sample < samples; ++sample) {
            source = activeCase == 3 ? Scalar::fromF32(numbers<float>[sample]) : Scalar::fromF64(numbers<double>[sample]);
            for (unsigned pattern = 0; pattern < 2; ++pattern) {
                for (unsigned rep = 0; rep < repetitions; ++rep) {
                    const unsigned char fill = pattern == 0 ? 0xa5 : 0x5a;
                    for (auto& byte : probeStack) byte = fill;
                    output[0] = '\0';
                    const auto primask = __get_PRIMASK();
                    const auto control = __get_CONTROL();
                    const auto psp = __get_PSP();
                    __disable_irq();
                    // Switching occurs entirely in assembly, with no C++ locals
                    // on the stack being repurposed. The callback preserves AAPCS.
                    const auto length = telemetry_call_on_psp(&exercise,
                        const_cast<unsigned char*>(probeStack) + sizeof(probeStack));
                    const bool restored = __get_CONTROL() == control && __get_PSP() == psp;
                    __set_PRIMASK(primask);
                    unsigned first = 0;
                    while (first < sizeof(probeStack) && probeStack[first] == fill) ++first;
                    const unsigned used = sizeof(probeStack) - first;
                    if (!restored || first < guardBytes || length >= sizeof(output)
                        || (activeCase == 0 && (used < 512 || length != 0))
                        || (activeCase >= 1 && activeCase <= 5 && (length == 0 || output[length] != '\0'))
                        || (activeCase >= 6 && length != 0)) {
                        line("STACK FAIL measurement\r\n"); return;
                    }
                    std::snprintf(report, sizeof(report), "STACK T %u %u %u %u %u %lu %lu\r\n",
                        activeCase, sample, pattern, rep, used, static_cast<unsigned long>(length),
                        static_cast<unsigned long>(checksum(length)));
                    line(report);
                    if (length != 0) { line("STACK JSON "); line(output); line("\r\n"); }
                }
            }
        }
    }
    line("STACK DONE\r\n");
}
