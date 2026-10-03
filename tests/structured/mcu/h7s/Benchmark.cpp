/* DWT and independent DTCM stack observations. Authors: Ruslan Kovtun
 * (shpegun60), codexAi. SPDX-License-Identifier: MIT. */
#include "../Fixture.hpp"
#include "usart.h"
#include "uart_bench.h"
#include <cstdio>
#include <cstring>

BenchCounter g_bench_usart_irq{}, g_bench_rx_dma_irq{}, g_bench_tx_dma_irq{};
extern "C" std::size_t telemetry_call_on_psp(std::size_t (*)() noexcept, void*) noexcept;
extern "C" [[noreturn]] void abort() noexcept { __builtin_trap(); }
namespace mcu::h7s {
#ifndef MCU_SCALE
extern const Operation nativeOperation;
unsigned valuesBytes() noexcept;
#endif
void checkExtra(unsigned&, unsigned&) noexcept;
}

namespace {
constexpr unsigned stackBytes = 16384, guardBytes = 256, stackRepeats = 3;
#ifdef MCU_SCALE
constexpr unsigned family = 1, expectedChecks = 2316, expectedConsumers = 0, expectedExtra = 0;
#else
constexpr unsigned family = 0, expectedChecks = 12230, expectedConsumers = 97, expectedExtra = 3;
#endif
alignas(32) __attribute__((section(".mcu_probe_stack")))
volatile unsigned char probeStack[guardBytes + stackBytes];
alignas(32) __attribute__((section(".dtcm_ids")))
std::array<std::uint32_t, mcu::sequenceSize> ids;
mcu::Probe activeProbe;
unsigned activeIterations;
bool ready;

unsigned operationCount() noexcept
{
    return mcu::operations().size() + (family == 0 ? 1u : 0u);
}
const mcu::Operation& operation(unsigned index) noexcept
{
#ifndef MCU_SCALE
    if (index == mcu::operations().size()) return mcu::h7s::nativeOperation;
#endif
    return mcu::operations()[index];
}
std::uint32_t expected(unsigned index, std::uint32_t id) noexcept
{
#ifndef MCU_SCALE
    if (index == mcu::operations().size()) return 4097u;
#endif
    return mcu::expected(index, id);
}
void line(const char* text) noexcept
{
    if (HAL_UART_Transmit(&huart3, reinterpret_cast<const std::uint8_t*>(text),
                         static_cast<std::uint16_t>(std::strlen(text)), 5000) != HAL_OK) Error_Handler();
}
__attribute__((noinline)) std::uint32_t window(mcu::Probe probe, unsigned count) noexcept
{
    std::uint32_t sum = 0;
    for (unsigned i = 0; i < count; ++i) sum += probe(ids[i & (mcu::sequenceSize - 1)]);
    return sum;
}
__attribute__((noinline)) std::size_t stackExercise() noexcept
{
    return window(activeProbe, activeIterations);
}
__attribute__((noinline)) std::size_t stackControl() noexcept
{
    volatile unsigned char control[512];
    for (unsigned i = 0; i < sizeof(control); ++i) control[i] = static_cast<unsigned char>(i);
    return control[511] == 255 ? 0 : 1;
}
std::uint32_t expectedSum(unsigned index, unsigned count) noexcept
{
    std::uint32_t sum = 0;
    for (unsigned i = 0; i < count; ++i) sum += expected(index, ids[i & (mcu::sequenceSize - 1)]);
    return sum;
}
bool stackSample(int index, unsigned profile, unsigned pattern, unsigned repetition,
                 std::uint32_t expectedValue, bool control) noexcept
{
    const unsigned char fill = pattern == 0 ? 0xa5 : 0x5a;
    for (auto& byte : probeStack) byte = fill;
    const auto primask = __get_PRIMASK(), oldControl = __get_CONTROL(), oldPsp = __get_PSP();
    __disable_irq();
    const auto checksum = telemetry_call_on_psp(control ? stackControl : stackExercise,
        const_cast<unsigned char*>(probeStack) + sizeof(probeStack));
    const bool restored = __get_CONTROL() == oldControl && __get_PSP() == oldPsp;
    __set_PRIMASK(primask);
    unsigned first = 0;
    while (first < sizeof(probeStack) && probeStack[first] == fill) ++first;
    const unsigned used = sizeof(probeStack) - first;
    if (!restored || first < guardBytes || checksum != expectedValue || (control && used < 512)) {
        line("MCU FAIL stack\r\n"); return false;
    }
    char report[128];
    std::snprintf(report, sizeof(report), "MCU S %d %u %u %u %u %lu\r\n",
        index, profile, pattern, repetition, used, static_cast<unsigned long>(checksum));
    line(report);
    return true;
}
} // namespace

extern "C" void bench_init()
{
    // The Cube default map permits speculation into the absent GFXMMU block.
    // Mark it Device/no-access before enabling either cache.
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
    if (!ready) { line("MCU FAIL setup\r\n"); return; }
    telemetry::requireStructuredAbi();
    qualification::checks = qualification::failures = 0;
#ifndef MCU_SCALE
    qualification::consumer_typed(); qualification::consumer_encoded();
#endif
    const unsigned consumers = qualification::checks;
    mcu::checkProbes();
    unsigned extra = 0, extraFailures = 0;
    mcu::h7s::checkExtra(extra, extraFailures);
    mcu::prepare();
    SCB->CSSELR = 0; __DSB();
    const auto cache = SCB->CCSIDR;
    const auto cacheBytes = (((cache >> 13) & 0x7fff) + 1) * (((cache >> 3) & 0x3ff) + 1) * (1u << ((cache & 7) + 4));
    char report[240];
    std::snprintf(report, sizeof(report), "MCU READY 1 %u %u %lu %lu %u %u %u %u %u %u %u\r\n",
        family, LAYOUT_OPT, static_cast<unsigned long>(SystemCoreClock), static_cast<unsigned long>(cacheBytes),
        stackBytes, guardBytes, mcu::rows, mcu::sequenceSize, mcu::repeats, stackRepeats, operationCount());
    line(report);
    std::snprintf(report, sizeof(report), "MCU MEMORY %lu %lu %lu %u %u %u %u %u\r\n",
        static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(probeStack)),
        static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(ids.data())),
        static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(operation(0).invoke)),
        unsigned(sizeof(mcu::ts::ModelView)), unsigned(sizeof(mcu::ts::FieldEntry)),
        unsigned(sizeof(mcu::ts::CommandEntry)), unsigned(sizeof(mcu::ts::ServiceEntry)), unsigned(sizeof(fixture::Big)));
    line(report);
    std::snprintf(report, sizeof(report), "MCU CORRECT %u %u %u %u %u\r\n",
        qualification::checks, qualification::failures, consumers, extra, extraFailures);
    line(report);
    if (qualification::checks != expectedChecks || qualification::failures != 0 ||
        consumers != expectedConsumers || extra != expectedExtra || extraFailures != 0) {
        line("MCU FAIL correctness\r\n"); return;
    }
#ifndef MCU_SCALE
    const auto descriptor = qualification::descriptorBytes();
    const auto fingerprint = qualification::fingerprint();
    std::snprintf(report, sizeof(report), "MCU DESCRIPTOR %u %u %08lx%08lx\r\n",
        unsigned(descriptor.size()), mcu::h7s::valuesBytes(),
        static_cast<unsigned long>(fingerprint >> 32), static_cast<unsigned long>(fingerprint & UINT32_MAX));
    line(report);
    constexpr char hex[] = "0123456789abcdef";
    for (unsigned cursor = 0; cursor < descriptor.size(); cursor += 64) {
        const unsigned count = unsigned(descriptor.size()) - cursor < 64 ? unsigned(descriptor.size()) - cursor : 64;
        char bytes[129];
        for (unsigned i = 0; i < count; ++i) {
            const auto value = std::to_integer<unsigned>(descriptor[cursor + i]);
            bytes[2 * i] = hex[value >> 4]; bytes[2 * i + 1] = hex[value & 15u];
        }
        bytes[2 * count] = '\0';
        std::snprintf(report, sizeof(report), "MCU D %u %s\r\n", cursor, bytes); line(report);
    }
#endif
    for (unsigned index = 0; index < operationCount(); ++index) {
        const auto& op = operation(index);
        std::uint32_t sums[3];
        for (unsigned profile = 0; profile < 3; ++profile) {
            mcu::sequence(profile, ids); sums[profile] = expectedSum(index, op.iterations);
        }
        std::snprintf(report, sizeof(report), "MCU OP %u %s %u %u %lu %lu %lu\r\n", index,
            op.name, op.profiles, op.iterations, static_cast<unsigned long>(sums[0]),
            static_cast<unsigned long>(sums[1]), static_cast<unsigned long>(sums[2]));
        line(report);
    }
    for (unsigned pattern = 0; pattern < 2; ++pattern)
        for (unsigned rep = 0; rep < stackRepeats; ++rep)
            if (!stackSample(-1, 0, pattern, rep, 0, true)) return;
    for (unsigned index = 0; index < operationCount(); ++index) {
        const auto& op = operation(index);
        for (unsigned profile = 0; profile < 3; ++profile) {
            if (!(op.profiles & (1u << profile))) continue;
            mcu::sequence(profile, ids);
            const auto sum = expectedSum(index, op.iterations);
            for (unsigned rep = 0; rep < mcu::repeats; ++rep) {
                const auto primask = __get_PRIMASK(); __disable_irq();
                SCB_CleanInvalidateDCache(); SCB_InvalidateICache();
                volatile auto warm = window(op.invoke, mcu::sequenceSize); (void)warm;
                __DSB(); __ISB();
                const auto start = DWT->CYCCNT;
                const auto result = window(op.invoke, op.iterations);
                __DSB(); const std::uint32_t cycles = DWT->CYCCNT - start;
                __set_PRIMASK(primask);
                if (result != sum || cycles == 0) { line("MCU FAIL timing\r\n"); return; }
                std::snprintf(report, sizeof(report), "MCU T %u %u %u %lu %u %lu\r\n",
                    index, profile, rep, static_cast<unsigned long>(cycles), op.iterations, static_cast<unsigned long>(result));
                line(report);
            }
            activeProbe = op.invoke; activeIterations = op.iterations;
            for (unsigned pattern = 0; pattern < 2; ++pattern)
                for (unsigned rep = 0; rep < stackRepeats; ++rep)
                    if (!stackSample(index, profile, pattern, rep, sum, false)) return;
        }
    }
    line("MCU DONE\r\n");
}
