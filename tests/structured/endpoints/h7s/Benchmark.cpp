/*
 * @file Benchmark.cpp
 * @brief DWT measurements with cacheable RAM, Flash, and preserved firmware.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#include "Fixture.hpp"
#include "main.h"
#include "usart.h"
#include <cstdio>
#include <cstring>

int structured_mixed_check();
int structured_large_check();
// Counters referenced by the unmodified Cube interrupt handlers.
extern "C" {
volatile std::uint32_t g_bench_usart_irq = 0;
volatile std::uint32_t g_bench_tx_dma_irq = 0;
volatile std::uint32_t g_bench_rx_dma_irq = 0;
}
namespace bench {
std::array<std::byte, 4> input{std::byte{17}, std::byte{0}, std::byte{0}, std::byte{0}};
}
namespace {
using namespace bench;
constexpr unsigned iterations = 16384, repetitions = 5;
alignas(32) std::array<ts::FieldEntry, large> ramFields;
alignas(32) std::array<ts::CommandEntry, large> ramCommands;
alignas(32) std::array<ts::ServiceEntry, large> ramServices;
// Older entries load a separate definition before reaching Device. Direct
// contexts have no such indirection; both variants call the same Device.
#ifndef ENDPOINT_DIRECT_CONTEXTS
std::array<std::optional<std::remove_cv_t<decltype(field)>>, large> fieldDefinitions;
std::array<std::optional<std::remove_cv_t<decltype(command)>>, large> commandDefinitions;
std::array<std::optional<std::remove_cv_t<decltype(service)>>, large> serviceDefinitions;
#endif
__attribute__((section(".dtcm_ids"))) std::array<std::uint32_t, large> ids;
bool ready = false;
#ifdef ENDPOINT_LEGACY
using OldFields = std::array<telemetry::Field, large>;
using OldCommands = std::array<telemetry::Command, large>;
alignas(OldFields) std::byte oldFieldStorage[sizeof(OldFields)];
alignas(OldCommands) std::byte oldCommandStorage[sizeof(OldCommands)];
OldFields* oldRamFields = nullptr;
OldCommands* oldRamCommands = nullptr;
#endif
void line(const char* text)
{
    HAL_UART_Transmit(&huart3, reinterpret_cast<const std::uint8_t*>(text),
                      static_cast<std::uint16_t>(std::strlen(text)), 1000);
}
void sequence(unsigned profile, unsigned count)
{
    for (unsigned i = 0; i < large; ++i) ids[i] = profile == 0 ? 0 : i % count;
    if (profile == 2) {
        std::uint32_t state = 0x51d724b;
        for (unsigned i = large - 1; i; --i) {
            state ^= state << 13; state ^= state >> 17; state ^= state << 5;
            const auto j = state % (i + 1);
            const auto value = ids[i]; ids[i] = ids[j]; ids[j] = value;
        }
    }
}
__attribute__((noinline))
std::uint32_t window(Probe probe, const void* index, ts::Workspace& workspace, unsigned count)
{
    std::uint32_t sum = 0;
    for (unsigned i = 0; i < count; ++i) sum += probe(index, ids[i & (large - 1)], workspace);
    return sum;
}
void measure(unsigned memory, unsigned count)
{
    const ts::FieldCatalog fields{"fields", memory == 0 ? flashFields.data() : ramFields.data(), count};
    const ts::CommandCatalog commands{"commands", memory == 0 ? flashCommands.data() : ramCommands.data(), count};
    const ts::ServiceCatalog services{"services", memory == 0 ? flashServices.data() : ramServices.data(), count};
    const ts::FieldIndex fieldIndex{&fields, 1};
    const ts::CommandIndex commandIndex{&commands, 1};
    const ts::ServiceIndex serviceIndex{&services, 1};
#ifdef ENDPOINT_LEGACY
    const telemetry::Catalog oldFields{"oldFields", memory == 0 ? oldFlashFields.data() : oldRamFields->data(), count};
    const telemetry::CommandCatalog oldCommands{"oldCommands", memory == 0 ? oldFlashCommands.data() : oldRamCommands->data(), count};
    const telemetry::CatalogIndex oldFieldIndex{&oldFields, 1};
    const telemetry::CommandCatalogIndex oldCommandIndex{&oldCommands, 1};
    const void* indexes[]{&fieldIndex, &fieldIndex, &commandIndex, &serviceIndex,
        &oldFieldIndex, &oldFieldIndex, &oldCommandIndex, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
#ifdef ENDPOINT_COMPONENTS
        nullptr, &fieldIndex, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
#endif
#ifdef ENDPOINT_STORAGE
        fieldIndex.find(0u), fieldIndex.find(0u), commandIndex.find(0u),
        sizeIndexes[0], sizeIndexes[1], sizeIndexes[2], sizeIndexes[3],
        sizeIndexes[4], sizeIndexes[5], sizeIndexes[6], sizeIndexes[7],
        sizeIndexes[8], sizeIndexes[9], sizeIndexes[10], sizeIndexes[11],
#endif
    };
    const Probe probes[]{&bench::read, &bench::write, &bench::execute, &bench::call,
        &bench::oldRead, &bench::oldWrite, &bench::oldExecute, &bench::oldTypedRead, &bench::newTypedRead,
        &bench::oldTypedWrite, &bench::newTypedWrite, &bench::oldTypedCall, &bench::newTypedCall,
#ifdef ENDPOINT_COMPONENTS
        &bench::componentLocal, &bench::componentFind, &bench::componentPreflight,
        &bench::componentReserveScalar, &bench::componentReserveStruct, &bench::componentDecode,
        &bench::componentDecodeStruct, &bench::componentEncode, &bench::componentCallback,
        &bench::componentOverlap,
#endif
#ifdef ENDPOINT_STORAGE
        &resolvedRead, &resolvedWrite, &resolvedCommand,
        sizeProbes[0], sizeProbes[1], sizeProbes[2], sizeProbes[3],
        sizeProbes[4], sizeProbes[5], sizeProbes[6], sizeProbes[7],
        sizeProbes[8], sizeProbes[9], sizeProbes[10], sizeProbes[11],
#endif
    };
#else
    const void* indexes[]{&fieldIndex, &fieldIndex, &commandIndex, &serviceIndex};
    const Probe probes[]{&bench::read, &bench::write, &bench::execute, &bench::call};
#endif
    std::array<std::byte, 256> scratch{};
    ts::Workspace workspace{scratch};
    char output[160];
    for (unsigned profile = 0; profile < 3; ++profile) {
        sequence(profile, count);
        for (unsigned op = 0; op < std::size(probes); ++op) {
            if (op >= 7 && (memory != 0 || profile != 0)) continue;
            for (unsigned rep = 0; rep < repetitions; ++rep) {
                SCB_CleanInvalidateDCache(); SCB_InvalidateICache(); __DSB(); __ISB();
                const auto mask = __get_PRIMASK(); __disable_irq();
                volatile auto warm = window(probes[op], indexes[op], workspace, 2048);
                (void)warm;
                __DSB(); __ISB();
                const auto begin = DWT->CYCCNT;
                const auto sum = window(probes[op], indexes[op], workspace, iterations);
                __DSB(); __ISB();
                const auto elapsed = DWT->CYCCNT - begin;
                __set_PRIMASK(mask);
                std::snprintf(output, sizeof(output), "STRUCT T %u %u %u %u %lu %lu\r\n",
                    memory, profile, op, rep, static_cast<unsigned long>(elapsed), static_cast<unsigned long>(sum));
                line(output);
            }
        }
    }
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
    ready = SystemCoreClock == 600000000u;
}
extern "C" void bench_loop()
{
    std::uint8_t instruction = 0;
    if (HAL_UART_Receive(&huart3, &instruction, 1, 100) != HAL_OK) return;
    if (instruction == 'P') { line("STRUCT IDLE 1\r\n"); return; }
    if (instruction != 'R') return;
    if (!ready) { line("STRUCT FAIL setup\r\n"); return; }
    const auto mixed = structured_mixed_check();
    const auto big = structured_large_check();
    char report[160];
    std::snprintf(report, sizeof(report), "STRUCT CHECK %d %d\r\n", mixed, big);
    line(report);
    if (mixed || big) return;
    std::snprintf(report, sizeof(report), "STRUCT POLICY %u\r\n", unsigned(ts::maxLocalObjectBytes));
    line(report);
#ifdef ENDPOINT_LEGACY
    oldRamFields = ::new (static_cast<void*>(oldFieldStorage)) OldFields;
    oldRamCommands = ::new (static_cast<void*>(oldCommandStorage)) OldCommands;
    for (unsigned i = 0; i < large; ++i) {
        (*oldRamFields)[i].~Field();
        ::new (static_cast<void*>(&(*oldRamFields)[i])) telemetry::Field(oldFieldSeed.data()[0]);
        (*oldRamCommands)[i].~Command();
        ::new (static_cast<void*>(&(*oldRamCommands)[i])) telemetry::Command(oldCommandSeed.data()[0]);
    }
    oldRamFields = std::launder(oldRamFields);
    oldRamCommands = std::launder(oldRamCommands);
    std::snprintf(report, sizeof(report), "STRUCT LEGACY %u %u %u %u\r\n",
        unsigned(sizeof(telemetry::Field)), unsigned(alignof(telemetry::Field)),
        unsigned(sizeof(telemetry::Command)), unsigned(alignof(telemetry::Command)));
    line(report);
#endif
    for (unsigned i = 0; i < large; ++i) {
#ifdef ENDPOINT_DIRECT_CONTEXTS
        ramFields[i] = fieldSeed.data()[0];
        ramCommands[i] = commandSeed.data()[0];
        ramServices[i] = serviceSeed.data()[0];
#else
        fieldDefinitions[i].emplace(field); commandDefinitions[i].emplace(command); serviceDefinitions[i].emplace(service);
        ramFields[i] = fieldSeed.data()[0]; ramFields[i].definition = &*fieldDefinitions[i];
        ramCommands[i] = commandSeed.data()[0]; ramCommands[i].definition = &*commandDefinitions[i];
        ramServices[i] = serviceSeed.data()[0]; ramServices[i].definition = &*serviceDefinitions[i];
#endif
    }
    SCB->CSSELR = 0; __DSB();
    const auto cache = SCB->CCSIDR;
    const auto cacheBytes = (((cache >> 13) & 0x7fff) + 1) * (((cache >> 3) & 0x3ff) + 1) * (1u << ((cache & 7) + 4));
    std::snprintf(report, sizeof(report), "STRUCT READY %lu %lu %u %u %u %u %u %u %u\r\n",
        static_cast<unsigned long>(SystemCoreClock), static_cast<unsigned long>(cacheBytes),
        unsigned(sizeof(ts::FieldEntry)), unsigned(alignof(ts::FieldEntry)),
        unsigned(sizeof(ts::CommandEntry)), unsigned(alignof(ts::CommandEntry)),
        unsigned(sizeof(ts::ServiceEntry)), unsigned(alignof(ts::ServiceEntry)), iterations);
    line(report);
    measure(0, small); measure(1, small); measure(2, large);
#ifdef ENDPOINT_LEGACY
    oldRamFields->~OldFields(); oldRamCommands->~OldCommands();
#endif
    line("STRUCT DONE\r\n");
}
