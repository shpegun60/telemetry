/* DWT windows and independent PSP observations; UART is outside measurements.
 * Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT. */
#include "Fixture.hpp"
#include "usart.h"
#include "uart_bench.h"
#include <cstdio>
#include <cstring>

BenchCounter g_bench_usart_irq{}, g_bench_rx_dma_irq{}, g_bench_tx_dma_irq{};
extern "C" std::size_t telemetry_call_on_psp(std::size_t (*)() noexcept, void*) noexcept;
extern "C" [[noreturn]] void abort() noexcept { __builtin_trap(); }
namespace {
namespace b = borrowed_h7s;
constexpr unsigned stackBytes = 16384, guardBytes = 256;
#ifdef __OPTIMIZE_SIZE__
constexpr unsigned optimization = 0;
#else
constexpr unsigned optimization = 2;
#endif
static_assert(sizeof(b::ts::BorrowedServiceResult<b::Small>) == 8);
static_assert(sizeof(b::ts::BorrowedServiceResult<b::Large>) == 8);
static_assert(sizeof(b::ts::ServiceResult<b::Small>) == 4097);
alignas(32) __attribute__((section(".dtcm_ids")))
volatile unsigned char probeStack[stackBytes + guardBytes];
alignas(32) __attribute__((section(".dtcm_ids")))
std::array<std::uint32_t, b::sequenceSize> ids;
b::Probe activeProbe;
unsigned activeIterations;
bool ready;
void line(const char* text) noexcept
{
    if (HAL_UART_Transmit(&huart3, reinterpret_cast<const std::uint8_t*>(text),
        static_cast<std::uint16_t>(std::strlen(text)), 5000) != HAL_OK) Error_Handler();
}
__attribute__((noinline)) std::uint32_t window(b::Probe probe, unsigned count) noexcept
{
    std::uint32_t sum = 0;
    for (unsigned i = 0; i < count; ++i) sum += probe(ids[i & (b::sequenceSize - 1)]);
    return sum;
}
__attribute__((noinline)) std::size_t stackExercise() noexcept
{ return window(activeProbe, activeIterations); }
__attribute__((noinline)) std::size_t stackControl() noexcept
{
    volatile unsigned char value[512];
    for (unsigned i = 0; i < sizeof(value); ++i) value[i] = static_cast<unsigned char>(i);
    return value[511] == 255 ? 0 : 1;
}
bool stackSample(int op, unsigned profile, unsigned pattern, unsigned repetition,
                 std::uint32_t expected, bool control) noexcept
{
    const unsigned char fill = pattern == 0 ? 0xa5 : 0x5a;
    for (auto& value : probeStack) value = fill;
    const auto primask = __get_PRIMASK(), oldControl = __get_CONTROL(), oldPsp = __get_PSP();
    b::callbacks = 0;
    __disable_irq();
    const auto sum = telemetry_call_on_psp(control ? stackControl : stackExercise,
        const_cast<unsigned char*>(probeStack) + sizeof(probeStack));
    const bool restored = __get_CONTROL() == oldControl && __get_PSP() == oldPsp;
    __set_PRIMASK(primask);
    unsigned first = 0;
    while (first < sizeof(probeStack) && probeStack[first] == fill) ++first;
    const unsigned used = sizeof(probeStack) - first;
    const auto calls = b::callbacks;
    if (!restored || first < guardBytes || sum != expected ||
        calls != (control ? 0 : activeIterations) || (control && used < 512)) {
        line("BR FAIL stack\r\n"); return false;
    }
    char report[160];
    std::snprintf(report, sizeof(report), "BR S %d %u %u %u %u %lu %lu\r\n", op,
        profile, pattern, repetition, used, static_cast<unsigned long>(sum), static_cast<unsigned long>(calls));
    line(report); return true;
}
bool timingSample(unsigned index, unsigned profile, unsigned repetition) noexcept
{
    const auto& op = b::operations[index];
    const auto expected = b::expectedSum(op.bytes, op.iterations, ids);
    const auto primask = __get_PRIMASK(); __disable_irq();
    SCB_CleanInvalidateDCache(); SCB_InvalidateICache();
    b::callbacks = 0;
    const auto warm = window(op.invoke, b::sequenceSize);
    const bool warmed = warm == b::expectedSum(op.bytes, b::sequenceSize, ids) && b::callbacks == b::sequenceSize;
    b::callbacks = 0;
    __DSB(); __ISB();
    std::uint64_t cycles = 0;
    std::uint32_t sum = 0, previous = DWT->CYCCNT;
    bool bounded = true;
    for (unsigned i = 0; i < op.iterations; ++i) {
        sum += op.invoke(ids[i & (b::sequenceSize - 1)]);
        __DSB();
        const auto now = DWT->CYCCNT;
        const std::uint32_t delta = now - previous;
        previous = now;
        cycles += delta; // Wide accumulation and an enforced early window bound.
        if (delta > b::maximumCallCycles || cycles > b::maximumWindowCycles) { bounded = false; break; }
    }
    const auto calls = b::callbacks;
    __set_PRIMASK(primask);
    if (!warmed || !bounded || cycles == 0 || sum != expected || calls != op.iterations) {
        line("BR FAIL timing\r\n"); return false;
    }
    char report[160];
    std::snprintf(report, sizeof(report), "BR T %u %u %u %lu %u %lu %lu\r\n", index,
        profile, repetition, static_cast<unsigned long>(cycles), op.iterations,
        static_cast<unsigned long>(sum), static_cast<unsigned long>(calls));
    line(report); return true;
}
}

extern "C" void bench_init()
{
    MPU_Region_InitTypeDef region{};
    HAL_MPU_Disable();
    region.Enable = MPU_REGION_ENABLE; region.Number = MPU_REGION_NUMBER1;
    region.BaseAddress = 0x24000000; region.Size = MPU_REGION_SIZE_512KB;
    region.AccessPermission = MPU_REGION_FULL_ACCESS; region.TypeExtField = MPU_TEX_LEVEL1;
    region.IsCacheable = MPU_ACCESS_CACHEABLE; region.IsBufferable = MPU_ACCESS_BUFFERABLE;
    region.IsShareable = MPU_ACCESS_NOT_SHAREABLE; region.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
    HAL_MPU_ConfigRegion(&region);
    region.Number = MPU_REGION_NUMBER2; region.BaseAddress = 0x25000000;
    region.Size = MPU_REGION_SIZE_16MB; region.AccessPermission = MPU_REGION_NO_ACCESS;
    region.TypeExtField = MPU_TEX_LEVEL0; region.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
    region.IsBufferable = MPU_ACCESS_BUFFERABLE; region.IsShareable = MPU_ACCESS_SHAREABLE;
    HAL_MPU_ConfigRegion(&region); HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
    SCB_EnableICache(); SCB_EnableDCache();
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0; DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    __DSB(); __ISB();
    ready = SystemCoreClock == 600000000u && __get_IPSR() == 0 && (__get_CONTROL() & 3u) == 0 &&
        (SCB->CCR & (SCB_CCR_IC_Msk | SCB_CCR_DC_Msk)) == (SCB_CCR_IC_Msk | SCB_CCR_DC_Msk);
}

extern "C" void bench_loop()
{
    std::uint8_t command = 0;
    if (HAL_UART_Receive(&huart3, &command, 1, 100) != HAL_OK || command != 'R') return;
    if (!ready) { line("BR FAIL setup\r\n"); return; }
    b::ts::requireStructuredAbi();
    b::prepare();
    const auto checks = b::check();
    SCB->CSSELR = 0; __DSB();
    const auto cache = SCB->CCSIDR;
    const auto cacheBytes = (((cache >> 13) & 0x7fff) + 1) * (((cache >> 3) & 0x3ff) + 1) * (1u << ((cache & 7) + 4));
    char report[320];
    std::snprintf(report, sizeof(report), "BR READY 1 %u %lu %lu %u %u %u %u %u %u\r\n",
        optimization, static_cast<unsigned long>(SystemCoreClock),
        static_cast<unsigned long>(cacheBytes), stackBytes, guardBytes, b::sequenceSize,
        b::timingRepeats, b::stackRepeats, b::operationCount);
    line(report);
    const auto address = [](const void* value) { return static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(value)); };
    std::snprintf(report, sizeof(report), "BR MEMORY %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %u %u %u\r\n",
        address(const_cast<unsigned char*>(probeStack)), address(const_cast<unsigned char*>(probeStack) + sizeof(probeStack)),
        address(b::cache4.data()), address(b::cache4.data()) + sizeof(b::cache4),
        address(b::cache64.data()), address(b::cache64.data()) + sizeof(b::cache64),
        address(b::output.data()), address(b::output.data()) + sizeof(b::output),
        address(b::scratch.data()), address(b::scratch.data()) + sizeof(b::scratch),
        unsigned(sizeof(b::ts::ServiceResult<b::Small>)), unsigned(sizeof(b::ts::BorrowedServiceResult<b::Small>)),
        unsigned(sizeof(b::ts::BorrowedServiceResult<b::Large>)));
    line(report);
    std::snprintf(report, sizeof(report), "BR CORRECT %u %u %u\r\n", checks.checked, checks.failed, checks.payloadBytes);
    line(report);
    if (checks.checked != b::expectedChecks || checks.failed || checks.payloadBytes != b::expectedPayloadBytes) {
        line("BR FAIL correctness\r\n"); return;
    }
    for (unsigned index = 0; index < b::operationCount; ++index) {
        const auto& op = b::operations[index];
        std::uint32_t sums[3];
        for (unsigned profile = 0; profile < 3; ++profile) {
            b::sequence(profile, ids); sums[profile] = b::expectedSum(op.bytes, op.iterations, ids);
        }
        std::snprintf(report, sizeof(report), "BR OP %u %s %u %u %u %lu %lu %lu\r\n", index,
            op.name, op.bytes, 7u, op.iterations, static_cast<unsigned long>(sums[0]),
            static_cast<unsigned long>(sums[1]), static_cast<unsigned long>(sums[2]));
        line(report);
    }
    for (unsigned pattern = 0; pattern < 2; ++pattern)
        for (unsigned rep = 0; rep < b::stackRepeats; ++rep)
            if (!stackSample(-1, 0, pattern, rep, 0, true)) return;
    for (unsigned index = 0; index < b::operationCount; ++index) {
        const auto& op = b::operations[index];
        for (unsigned profile = 0; profile < 3; ++profile) {
            b::sequence(profile, ids);
            for (unsigned rep = 0; rep < b::timingRepeats; ++rep)
                if (!timingSample(index, profile, rep)) return;
            activeProbe = op.invoke; activeIterations = op.iterations;
            const auto sum = b::expectedSum(op.bytes, op.iterations, ids);
            for (unsigned pattern = 0; pattern < 2; ++pattern)
                for (unsigned rep = 0; rep < b::stackRepeats; ++rep)
                    if (!stackSample(index, profile, pattern, rep, sum, false)) return;
        }
    }
    line("BR DONE\r\n");
}
