// Author: Ruslan Kovtun (shpegun60), codexAi. License: MIT.
// DWT measurements; UART output and fixture setup are outside timed windows.
#include "../Fixture.h"
#include "usart.h"
#include "uart_bench.h"
#include <cstdio>
#include <cstring>
#include <new>

using namespace telemetry;
alignas(32) volatile float layout_source_f32 = 230;
alignas(32) volatile double layout_source_f64 = 12.75;
alignas(32) volatile std::uint16_t layout_source_u16 = 17;
alignas(32) volatile float layout_sink_f32 = 0;
alignas(32) volatile std::uint16_t layout_sink_u16 = 0;
BenchCounter g_bench_usart_irq{}, g_bench_rx_dma_irq{}, g_bench_tx_dma_irq{};

extern const std::array<Field, LAYOUT_FIELD_COUNT> layout_fields;
extern const CatalogIndex layout_index;
extern "C" {
const Field* layout_find_runtime(const CatalogIndex&, FieldId) noexcept;
Scalar layout_read_runtime(const CatalogIndex&, FieldId) noexcept;
float layout_read_runtime_float(const CatalogIndex&, FieldId) noexcept;
WriteResult layout_write_runtime_float(const CatalogIndex&, FieldId, float) noexcept;
WriteResult layout_write_runtime_u16(const CatalogIndex&, FieldId, std::uint16_t) noexcept;
float layout_read_known() noexcept;
WriteResult layout_write_known(float) noexcept;
}

namespace {
constexpr unsigned ramCount = 1024, sequenceCount = 1024, iterations = 32768, repetitions = 5;
using RamFields = std::array<Field, ramCount>;
alignas(32) std::byte ramStorage[sizeof(RamFields)];
__attribute__((section(".dtcm_ids"), aligned(32))) std::uint32_t ids[sequenceCount];
bool ready = false;

void line(const char* text)
{
    if (HAL_UART_Transmit(&huart3, reinterpret_cast<const std::uint8_t*>(text),
                         static_cast<std::uint16_t>(std::strlen(text)), 2000) != HAL_OK) Error_Handler();
}

std::uint32_t bits(float value) noexcept
{
    std::uint32_t result;
    std::memcpy(&result, &value, sizeof(result));
    return result;
}

template<unsigned Op>
__attribute__((noinline)) std::uint32_t loop(const CatalogIndex& index, unsigned count) noexcept
{
    std::uint32_t sum = 0;
    for (unsigned i = 0; i < count; ++i) {
        const auto id = ids[i & (sequenceCount - 1)];
        if constexpr (Op == 0) sum += static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(layout_find_runtime(index, id)) >> 5);
        if constexpr (Op == 1) sum += static_cast<std::uint32_t>(layout_read_runtime(index, id).type());
        if constexpr (Op == 2) sum += bits(layout_read_runtime_float(index, id)) ^ id;
        if constexpr (Op == 3) sum += static_cast<std::uint32_t>(layout_write_runtime_float(index, id, 250.0f));
        if constexpr (Op == 4) sum += static_cast<std::uint32_t>(layout_write_runtime_u16(index, id, 17));
        if constexpr (Op == 5) sum += bits(layout_read_known()) ^ id;
        if constexpr (Op == 6) sum += static_cast<std::uint32_t>(layout_write_known(250.0f));
    }
    return sum;
}
using Loop = std::uint32_t (*)(const CatalogIndex&, unsigned) noexcept;
constexpr Loop loops[] = {loop<0>, loop<1>, loop<2>, loop<3>, loop<4>, loop<5>, loop<6>};

void sequence(unsigned profile, unsigned count)
{
    constexpr unsigned fixed[] = {0, 1, 2, 4};
    for (unsigned i = 0; i < sequenceCount; ++i) ids[i] = profile < 4 ? fixed[profile] : i % count;
    if (profile == 5) {
        std::uint32_t state = 0x19a753u;
        for (unsigned i = sequenceCount - 1; i > 0; --i) {
            state ^= state << 13; state ^= state >> 17; state ^= state << 5;
            const unsigned j = state % (i + 1);
            const auto temporary = ids[i]; ids[i] = ids[j]; ids[j] = temporary;
        }
    }
}

bool validate(const CatalogIndex& index, unsigned count)
{
    for (unsigned i = 0; i < count; ++i) {
        const auto value = index.read<float>(i);
        const float expected = i % 8 == 6 ? 12.0f : ((i % 8 == 0 || i % 8 == 1 || i % 8 == 7) ? 230.0f : 17.0f);
        if (!value || *value != expected || index.find(i)->id != i) return false;
    }
    return index.find(count) == nullptr && index.find(makeId(1, 0)) == nullptr;
}

void measure(const CatalogIndex& index, unsigned memory, unsigned count)
{
    char output[160];
    for (unsigned profile = 0; profile < 6; ++profile) {
        sequence(profile, count);
        for (unsigned op = 0; op < 7; ++op) {
            if (op >= 5 && (memory != 0 || profile != 0)) continue;
            for (unsigned rep = 0; rep < repetitions; ++rep) {
                const auto mask = __get_PRIMASK();
                __disable_irq();
                SCB_CleanInvalidateDCache();
                SCB_InvalidateICache();
                volatile auto warm = loops[op](index, 2048);
                (void)warm;
                __DSB(); __ISB();
                const std::uint32_t begin = DWT->CYCCNT;
                const auto checksum = loops[op](index, iterations);
                __DSB();
                const std::uint32_t elapsed = DWT->CYCCNT - begin;
                __set_PRIMASK(mask);
                std::snprintf(output, sizeof(output), "LAYOUT T %u %u %u %u %u %lu %lu\r\n",
                    memory, count, profile, op, rep, static_cast<unsigned long>(elapsed), static_cast<unsigned long>(checksum));
                line(output);
            }
        }
    }
}
} // namespace

extern "C" void bench_init()
{
    // The copied Cube setup leaves AXI SRAM on the architectural default map.
    // Explicitly select normal, non-shareable, write-back/write-allocate SRAM.
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
    __DSB(); __ISB();
    ready = SystemCoreClock == 600000000u && (SCB->CCR & (SCB_CCR_IC_Msk | SCB_CCR_DC_Msk)) == (SCB_CCR_IC_Msk | SCB_CCR_DC_Msk);
}

extern "C" void bench_loop()
{
    // Wait for the host after flashing; nothing is measured during UART I/O.
    std::uint8_t command = 0;
    if (HAL_UART_Receive(&huart3, &command, 1, 100) != HAL_OK || command != 'R') return;
    if (!ready) { line("LAYOUT FAIL setup\r\n"); return; }
    auto* table = ::new (static_cast<void*>(ramStorage)) RamFields;
    for (unsigned i = 0; i < ramCount; ++i) {
        // Replace the already-live array element before publishing any views.
        // Launder the final pointer because candidate C has const subobjects.
        (*table)[i].~Field();
        ::new (static_cast<void*>(&(*table)[i])) Field(layout_fixture::row(i));
    }
    const auto* fields = std::launder(table->data());
    const Catalog shortCatalog[] = {{0, "values", fields, LAYOUT_FIELD_COUNT}};
    const Catalog largeCatalog[] = {{0, "values", fields, ramCount}};
    const CatalogIndex shortIndex{shortCatalog}, largeIndex{largeCatalog};
    if (!validate(layout_index, LAYOUT_FIELD_COUNT) || !validate(shortIndex, LAYOUT_FIELD_COUNT) || !validate(largeIndex, ramCount)) {
        line("LAYOUT FAIL values\r\n"); return;
    }
    SCB->CSSELR = 0;
    __DSB();
    const auto cache = SCB->CCSIDR;
    const auto cacheBytes = (((cache >> 13) & 0x7fff) + 1) * (((cache >> 3) & 0x3ff) + 1) * (1u << ((cache & 7) + 4));
    char output[220];
    std::snprintf(output, sizeof(output), "LAYOUT READY 1 %u %u %lu %u %u %u %u %u %lu %lu %lu %u %u\r\n",
        TELEMETRY_LAYOUT_VARIANT, LAYOUT_OPT, static_cast<unsigned long>(SystemCoreClock),
        unsigned(sizeof(Field)), unsigned(alignof(Field)), unsigned(offsetof(Field, get)), unsigned(offsetof(Field, set)), unsigned(offsetof(Field, declaredType)),
        static_cast<unsigned long>(cacheBytes), static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(layout_fields.data())),
        static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(fields)), iterations, repetitions);
    line(output);
    measure(layout_index, 0, LAYOUT_FIELD_COUNT);
    measure(shortIndex, 1, LAYOUT_FIELD_COUNT);
    measure(largeIndex, 2, ramCount);
    table->~RamFields();
    line("LAYOUT DONE\r\n");
}
