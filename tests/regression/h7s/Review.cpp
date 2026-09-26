// On-device execution of the same review checks used by host CI.
// Authors: Ruslan Kovtun (shpegun60), codexAi. License: MIT.
#include "../EmbeddedReviewCheck.hpp"
#include "usart.h"
#include "uart_bench.h"
#include <cstdio>
#include <cstring>

BenchCounter g_bench_usart_irq{}, g_bench_rx_dma_irq{}, g_bench_tx_dma_irq{};

namespace {
void line(const char* text) noexcept
{
    HAL_UART_Transmit(&huart3,
        reinterpret_cast<const std::uint8_t*>(text), std::strlen(text), 1000);
}
}

extern "C" void bench_init()
{
    MPU_Region_InitTypeDef region{};
    HAL_MPU_Disable();
    region.Enable = MPU_REGION_ENABLE;
    region.Number = MPU_REGION_NUMBER1;
    region.BaseAddress = 0x24000000;
    region.Size = MPU_REGION_SIZE_512KB;
    region.AccessPermission = MPU_REGION_FULL_ACCESS;
    region.TypeExtField = MPU_TEX_LEVEL1;
    region.IsCacheable = MPU_ACCESS_CACHEABLE;
    region.IsBufferable = MPU_ACCESS_BUFFERABLE;
    region.IsShareable = MPU_ACCESS_NOT_SHAREABLE;
    region.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
    HAL_MPU_ConfigRegion(&region);

    // H7RS ES0596 2.2.17: prohibit speculative access to the unused GFXMMU
    // window. Reuse the established resource fixture's Device + XN mapping.
    region.Number = MPU_REGION_NUMBER2;
    region.BaseAddress = 0x25000000;
    region.Size = MPU_REGION_SIZE_16MB;
    region.TypeExtField = MPU_TEX_LEVEL0;
    region.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
    region.IsBufferable = MPU_ACCESS_BUFFERABLE;
    region.IsShareable = MPU_ACCESS_SHAREABLE;
    region.AccessPermission = MPU_REGION_NO_ACCESS;
    HAL_MPU_ConfigRegion(&region);
    HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
    SCB_EnableICache();
    SCB_EnableDCache();
    __DSB();
    __ISB();
}

extern "C" void bench_loop()
{
    std::uint8_t input = 0;
    if (HAL_UART_Receive(&huart3, &input, 1, 100) != HAL_OK) return;
    if (input == 'P') { line("REVIEW IDLE 1\r\n"); return; }
    if (input != 'R') return;
    if (SystemCoreClock != 600000000u || __get_IPSR() != 0 || (__get_CONTROL() & 3u) != 0 ||
        (SCB->CCR & (SCB_CCR_IC_Msk | SCB_CCR_DC_Msk)) != (SCB_CCR_IC_Msk | SCB_CCR_DC_Msk)) {
        line("REVIEW FAIL setup\r\n");
        return;
    }
    const auto result = review_embedded::run();
    char report[128];
    std::snprintf(report, sizeof report, "REVIEW RESULT 1 %u %lu %u %u %u\r\n",
        LAYOUT_OPT, static_cast<unsigned long>(SystemCoreClock),
        result.checks, result.failures, result.firstLine);
    line(report);
}
