/*
 * @file Benchmark.cpp
 * @brief Same-image DWT comparison; requests are issued only after UART R.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#include "../Fixture.hpp"
#include "main.h"
#include "usart.h"
#include <cstdio>
#include <cstring>
extern "C" {
volatile std::uint32_t g_bench_usart_irq = 0;
volatile std::uint32_t g_bench_tx_dma_irq = 0;
volatile std::uint32_t g_bench_rx_dma_irq = 0;
resource::ReadResult descriptor_stream(resource::Cursor, resource::Output) noexcept;
resource::ReadResult descriptor_packed(resource::Cursor, resource::Output) noexcept;
}
namespace {
using Reader = resource::ReadResult(*)(resource::Cursor, resource::Output) noexcept;
namespace df = descriptor_fixture;
static_assert(df::edge.size() == 1486 && df::edge.fingerprint() == 0xc380b060ffc7ce47ULL);
std::array<std::uint32_t, 256> positions;
void line(const char* text)
{ HAL_UART_Transmit(&huart3, reinterpret_cast<const std::uint8_t*>(text), std::strlen(text), 1000); }
std::uint32_t checksum(const std::byte* out, std::uint32_t count)
{ return count + std::to_integer<unsigned>(out[0]) + std::to_integer<unsigned>(out[count - 1]); }
__attribute__((noinline)) std::uint32_t window(Reader read, unsigned profile, unsigned iterations)
{
    std::array<std::byte, 256> out;
    std::uint32_t sum = 0;
    const unsigned sizes[] = {16, 64, 256, 64, 256};
    const auto output = std::span{out}.first(sizes[profile]);
    for (unsigned i = 0; i < iterations; ++i) {
        if (profile < 3) {
            const auto result = read(positions[i & 255], output);
            sum += checksum(out.data(), result.written);
        } else {
            resource::Cursor cursor = 0;
            do {
                const auto result = read(cursor, output);
                cursor = result.next;
                sum += checksum(out.data(), result.written);
            } while (cursor != df::edge.size());
        }
    }
    return sum;
}
int correctness()
{
    std::array<std::byte, 256> a, b;
    for (std::uint32_t offset = 0; offset < df::edge.size(); ++offset) {
        const auto x = descriptor_stream(offset, a);
        const auto y = descriptor_packed(offset, b);
        if (x.status != resource::Status::Ok || y.status != x.status || x.next != y.next ||
            x.written != y.written || x.eof != y.eof || std::memcmp(a.data(), b.data(), x.written)) return 1;
    }
    return df::calls || fixture::device.reads || fixture::device.commands || fixture::device.services ? 2 : 0;
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
    // Device/no-access GFXMMU region, established by the consolidated MCU bench.
    region.Number = MPU_REGION_NUMBER2; region.BaseAddress = 0x25000000;
    region.Size = MPU_REGION_SIZE_16MB; region.AccessPermission = MPU_REGION_NO_ACCESS;
    region.TypeExtField = MPU_TEX_LEVEL0; region.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
    region.IsBufferable = MPU_ACCESS_BUFFERABLE; region.IsShareable = MPU_ACCESS_SHAREABLE;
    HAL_MPU_ConfigRegion(&region); HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
    SCB_EnableICache(); SCB_EnableDCache();
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0; DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    std::uint32_t state = 0x93a17;
    for (auto& position : positions) { state = state * 1664525u + 1013904223u; position = state % df::edge.size(); }
}
extern "C" void bench_loop()
{
    std::uint8_t command = 0;
    if (HAL_UART_Receive(&huart3, &command, 1, 100) != HAL_OK) return;
    if (command == 'P') { line("DESC IDLE 1\r\n"); return; }
    if (command != 'R') return;
    char report[128];
    std::snprintf(report, sizeof(report), "DESC READY %lu %lu %u\r\n",
        static_cast<unsigned long>(SystemCoreClock), static_cast<unsigned long>(df::edge.size()),
        unsigned(decltype(df::edge)::indexBytes));
    line(report);
    const auto valid = correctness();
    std::snprintf(report, sizeof(report), "DESC CHECK %d\r\n", valid); line(report);
    if (valid != 0 || SystemCoreClock != 600000000u) return;
    const Reader readers[] = {descriptor_stream, descriptor_packed};
    for (unsigned profile = 0; profile < 5; ++profile) {
        for (unsigned variant = 0; variant < 2; ++variant) {
            const auto iterations = profile < 3 ? 4096u : 128u;
            for (unsigned rep = 0; rep < 5; ++rep) {
                SCB_CleanInvalidateDCache(); SCB_InvalidateICache(); __DSB(); __ISB();
                (void)window(readers[variant], profile, 32);
                const auto mask = __get_PRIMASK(); __disable_irq(); __DSB(); __ISB();
                const auto start = DWT->CYCCNT;
                const auto sum = window(readers[variant], profile, iterations);
                const auto elapsed = DWT->CYCCNT - start;
                __set_PRIMASK(mask);
                std::snprintf(report, sizeof(report), "DESC T %u %u %u %u %lu %lu\r\n",
                    variant, profile, rep, iterations, static_cast<unsigned long>(elapsed), static_cast<unsigned long>(sum));
                line(report);
            }
        }
    }
    line("DESC DONE\r\n");
}
