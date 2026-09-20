// DWT fixture for the three CommandTable execution levels on NUCLEO-H7S3L8.
// Author: Ruslan Kovtun (shpegun60), codexAi. License: MIT.
#include "Telemetry.h"
#include "usart.h"
#include "uart_bench.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

using namespace telemetry;

enum class DispatchMode : std::uint16_t { Off, Automatic, Manual };

struct DispatchOwner {
    CommandResult reset() noexcept;
    CommandResult configure(float voltage, DispatchMode mode) noexcept;
};

DispatchOwner dispatchOwner;
volatile std::uint32_t dispatchSink = 0;
volatile float dispatchVoltage = 230.0f;
volatile std::uint16_t dispatchMode = 1;
BenchCounter g_bench_usart_irq{}, g_bench_rx_dma_irq{}, g_bench_tx_dma_irq{};

__attribute__((noinline))
CommandResult DispatchOwner::reset() noexcept
{
    ++dispatchSink;
    return CommandResult::Executed;
}

__attribute__((noinline))
CommandResult DispatchOwner::configure(float, DispatchMode) noexcept
{
    ++dispatchSink;
    return CommandResult::Executed;
}

namespace {
constexpr CommandTable commands{
    command<&DispatchOwner::reset>(0, "Reset", dispatchOwner),
    command<&DispatchOwner::configure>(
        1, "Configure", dispatchOwner,
        arg<1>("Mode", "", DispatchMode::Automatic),
        arg<0>("Voltage", "V", 230.0f, 0.0f, 500.0f))};

constexpr unsigned iterations = 65536;
constexpr unsigned repetitions = 9;

__attribute__((noinline)) CommandResult callKnown(
    float voltage, DispatchMode mode) noexcept
{
    return commands.call<1>(voltage, mode);
}

__attribute__((noinline)) CommandResult callRuntime(
    std::size_t index, float voltage, DispatchMode mode) noexcept
{
    return commands.call(index, voltage, mode);
}

__attribute__((noinline)) CommandResult callErased(
    CommandId id, const Scalar* values) noexcept
{
    return commands.index().execute(id, values, 2);
}

void line(const char* text)
{
    if (HAL_UART_Transmit(&huart3, reinterpret_cast<const std::uint8_t*>(text),
                         static_cast<std::uint16_t>(std::strlen(text)), 2000) != HAL_OK)
        Error_Handler();
}

template <unsigned Operation>
__attribute__((noinline)) std::uint32_t loop(
    unsigned count, std::size_t runtimeIndex, CommandId runtimeId,
    float voltage, DispatchMode mode, const Scalar* erasedArguments) noexcept
{
    std::uint32_t sum = 0;
    for (unsigned i = 0; i < count; ++i) {
        CommandResult result{};
        if constexpr (Operation == 0)
            result = callKnown(voltage, mode);
        if constexpr (Operation == 1)
            result = callRuntime(runtimeIndex, voltage, mode);
        if constexpr (Operation == 2)
            result = callErased(runtimeId, erasedArguments);
        sum += static_cast<std::uint32_t>(result);
    }
    return sum ^ dispatchSink;
}

using Loop = std::uint32_t (*)(unsigned, std::size_t, CommandId, float,
                               DispatchMode, const Scalar*) noexcept;
constexpr Loop loops[] = {loop<0>, loop<1>, loop<2>};

bool validate() noexcept
{
    dispatchSink = 0;
    const float voltage = dispatchVoltage;
    const auto mode = static_cast<DispatchMode>(dispatchMode);
    const Scalar erased[] = {Scalar::fromF32(voltage),
                             Scalar::fromU16(static_cast<std::uint16_t>(mode))};
    return commands.call<1>(230.0f, DispatchMode::Automatic)
               == CommandResult::Executed
        && commands.call(std::size_t{1}, 230.0f, DispatchMode::Automatic)
               == CommandResult::Executed
        && commands.index().execute(1, erased, 2) == CommandResult::Executed
        && dispatchSink == 3;
}
} // namespace

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
    HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
    SCB_EnableICache();
    SCB_EnableDCache();
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    __DSB();
    __ISB();
}

extern "C" void bench_loop()
{
    std::uint8_t input = 0;
    if (HAL_UART_Receive(&huart3, &input, 1, 100) != HAL_OK || input != 'R') return;
    if (SystemCoreClock != 600000000u
        || (SCB->CCR & (SCB_CCR_IC_Msk | SCB_CCR_DC_Msk))
            != (SCB_CCR_IC_Msk | SCB_CCR_DC_Msk)
        || !validate()) {
        line("DISPATCH FAIL setup\r\n");
        return;
    }

    const float voltage = dispatchVoltage;
    const auto mode = static_cast<DispatchMode>(dispatchMode);
    const Scalar erased[] = {Scalar::fromF32(voltage),
                             Scalar::fromU16(static_cast<std::uint16_t>(mode))};
    char output[160];
    std::snprintf(output, sizeof(output),
        "DISPATCH READY 1 %u %lu %u %u %u %u\r\n",
        LAYOUT_OPT, static_cast<unsigned long>(SystemCoreClock),
        iterations, repetitions, unsigned(sizeof(Scalar)), unsigned(sizeof(Command)));
    line(output);

    for (unsigned operation = 0; operation < 3; ++operation) {
        for (unsigned repetition = 0; repetition < repetitions; ++repetition) {
            const auto mask = __get_PRIMASK();
            __disable_irq();
            SCB_CleanInvalidateDCache();
            SCB_InvalidateICache();
            dispatchSink = 0;
            volatile auto warm = loops[operation](4096, 1, 1, voltage, mode, erased);
            (void)warm;
            dispatchSink = 0;
            __DSB();
            __ISB();
            const std::uint32_t begin = DWT->CYCCNT;
            const auto checksum = loops[operation](
                iterations, 1, 1, voltage, mode, erased);
            __DSB();
            const std::uint32_t elapsed = DWT->CYCCNT - begin;
            __set_PRIMASK(mask);
            std::snprintf(output, sizeof(output),
                "DISPATCH T %u %u %lu %lu\r\n",
                operation, repetition,
                static_cast<unsigned long>(elapsed),
                static_cast<unsigned long>(checksum));
            line(output);
        }
    }
    line("DISPATCH DONE\r\n");
}
