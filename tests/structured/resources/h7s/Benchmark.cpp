/* Stage 10 MCU correctness and DWT windows, started only by UART R. MIT. */
#include "../Fixture.hpp"
#include "main.h"
#include "usart.h"
#include <cstdio>
#include <cstring>
extern "C" {
volatile std::uint32_t g_bench_usart_irq = 0;
volatile std::uint32_t g_bench_tx_dma_irq = 0;
volatile std::uint32_t g_bench_rx_dma_irq = 0;
}
namespace {
using namespace fixture;
#include "Golden.inc"
std::array<std::byte, bigValues.size()> buffer;
void line(const char* text)
{ HAL_UART_Transmit(&huart3, reinterpret_cast<const std::uint8_t*>(text), std::strlen(text), 1000); }

int correctness()
{
    for (unsigned cursor = 0; cursor <= 74; ++cursor) {
        for (unsigned capacity = 0; capacity <= 75; ++capacity) {
            calls.fill(0);
            buffer.fill(std::byte{0xcd});
            const auto result = values.read(cursor, {buffer.data(), capacity});
            bool valid = cursor < 24;
            for (auto offset : offsets) if (offset == cursor) valid = true;
            unsigned used = 0;
            std::array<unsigned, 12> wanted{};
            if (valid && cursor < 73) {
                if (cursor < 24) used = capacity < 24 - cursor ? capacity : 24 - cursor;
                for (unsigned i = 0; i < 10; ++i) {
                    if (offsets[i] != cursor + used) continue;
                    if (offsets[i + 1] - offsets[i] > capacity - used) break;
                    used += offsets[i + 1] - offsets[i];
                    if (i < 8) wanted[i] = 1;
                }
            }
            const auto status = !valid ? resource::Status::InvalidCursor :
                (cursor == 73 || used != 0 ? resource::Status::Ok : resource::Status::BufferTooSmall);
            if (result.status != status || result.written != used || result.next != cursor + used ||
                result.eof != (status == resource::Status::Ok && cursor + used == 73) || calls != wanted) return 1;
            if (used && std::memcmp(buffer.data(), golden.data() + cursor, used)) return 2;
            if (buffer[used] != std::byte{0xcd} || workspace.used() != 0) return 3;
        }
    }
    calls.fill(0);
    if (bigValues.read(29, {buffer.data(), 4096}).status != resource::Status::BufferTooSmall || calls[10]) return 4;
    if (!bigValues.read(0, buffer).eof || calls[0] != 1 || calls[10] != 1 || bigWorkspace.used()) return 5;
    if (buffer[30] != std::byte{4} || buffer[33] != std::byte{1}) return 6;
    for (unsigned i = 34; i < buffer.size(); ++i) if (buffer[i] != std::byte{0}) return 7;
    return 0;
}

__attribute__((noinline)) std::uint32_t window(unsigned profile, unsigned iterations)
{
    std::uint32_t sum = 0;
    for (unsigned i = 0; i < iterations; ++i) {
        const auto r = profile == 0 ? values.read(0, buffer) :
            profile == 1 ? values.read(43, {buffer.data(), 8}) : bigValues.read(0, buffer);
        sum += r.written + std::to_integer<unsigned>(buffer[0]) + std::to_integer<unsigned>(buffer[r.written - 1]);
    }
    return sum;
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
    HAL_MPU_ConfigRegion(&region); HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
    SCB_EnableICache(); SCB_EnableDCache();
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0; DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

extern "C" void bench_loop()
{
    std::uint8_t command = 0;
    if (HAL_UART_Receive(&huart3, &command, 1, 100) != HAL_OK) return;
    if (command == 'P') { line("VALUES IDLE 1\r\n"); return; }
    if (command != 'R') return;
    char report[128];
    std::snprintf(report, sizeof(report), "VALUES READY %lu %lu %lu\r\n",
        static_cast<unsigned long>(SystemCoreClock), static_cast<unsigned long>(fixture::values.size()),
        static_cast<unsigned long>(fixture::bigValues.size()));
    line(report);
    const auto valid = correctness();
    std::snprintf(report, sizeof(report), "VALUES CHECK %d\r\n", valid); line(report);
    if (valid != 0 || SystemCoreClock != 600000000u) return;
    for (unsigned profile = 0; profile < 3; ++profile) {
        const auto iterations = profile == 2 ? 256u : 4096u;
        for (unsigned rep = 0; rep < 5; ++rep) {
            SCB_CleanInvalidateDCache(); SCB_InvalidateICache(); __DSB(); __ISB();
            (void)window(profile, 32);
            const auto mask = __get_PRIMASK(); __disable_irq(); __DSB(); __ISB();
            const auto start = DWT->CYCCNT;
            const auto sum = window(profile, iterations);
            const auto elapsed = DWT->CYCCNT - start;
            __set_PRIMASK(mask);
            std::snprintf(report, sizeof(report), "VALUES T %u %u %u %lu %lu\r\n",
                profile, rep, iterations, static_cast<unsigned long>(elapsed), static_cast<unsigned long>(sum));
            line(report);
        }
    }
    line("VALUES DONE\r\n");
}
