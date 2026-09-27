/* Stage 11 MCU correctness and paired encoded/packet DWT windows. MIT. */
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
rs::Binding peer;
std::array<std::byte, model.maxScratch()> memory;
ts::Workspace workspace{memory};
std::array<std::byte, 64> input;
std::array<std::byte, 24 + ts::wireSize<Big> + 16> output;
std::array<std::byte, 16> bindRequest;
volatile std::uint32_t runtimeId = 0;
unsigned checked = 0;

void line(const char* text)
{ HAL_UART_Transmit(&huart3, reinterpret_cast<const std::uint8_t*>(text), std::strlen(text), 1000); }

int correctness()
{
    checked = 0;
    device = Device{};
    if (rs::Bind::process(peer, view, descriptor.fingerprint(), bindRequest, output).dispatch != D::Ok) return 1;
    const auto valid = packet(Op::Service, 0, Config{1234, true});
    for (unsigned length = 0; length <= 31; ++length) {
        for (unsigned capacity = 0; capacity <= 32; ++capacity) {
            for (unsigned alignment = 0; alignment < 4; ++alignment) {
                std::copy(valid.begin(), valid.end(), input.begin() + alignment);
                output.fill(std::byte{0xcd});
                const auto before = device.services;
                const auto result = rs::Exchange::process(peer, Input{input}.subspan(alignment, length),
                    Output{output}.subspan(3, capacity), workspace);
                const auto status = capacity < 24 ? D::BufferTooSmall : length < 24 ? D::InvalidRequest :
                    length != 29 ? D::InvalidPayload : capacity < 29 ? D::BufferTooSmall : D::Ok;
                const unsigned written = capacity < 24 || length < 24 ? 0 : status == D::Ok ? 29 : 24;
                if (result.dispatch != status || result.written != written || workspace.used() != 0 ||
                    device.services != before + unsigned(status == D::Ok)) return 2;
                if (output[2] != std::byte{0xcd} || output[3 + written] != std::byte{0xcd}) return 3;
                ++checked;
            }
        }
    }
    for (const auto op : {Op::FieldWrite, Op::Command, Op::Service}) {
        auto request = packet(op, 2, Config{1234, true});
        slot.reset(); const auto before = callbackCount();
        if (rs::Exchange::process(peer, request, output, workspace).dispatch != D::Unavailable) return 4;
        ++checked;
        request.back() = std::byte{255};
        if (rs::Exchange::process(peer, request, output, workspace).dispatch != D::InvalidPayload || callbackCount() != before) return 5;
        ++checked;
    }
    const auto large = packet(Op::Service, 3, Config{1234, true});
    const auto before = device.services;
    if (rs::Exchange::process(peer, large, Output{output}.first(4119), workspace).dispatch != D::BufferTooSmall ||
        device.services != before) return 6;
    ++checked;
    ts::Workspace shortScratch{Output{memory}.first(localServices.data()[3].scratchBytes - 1)};
    if (rs::Exchange::process(peer, large, output, shortScratch).dispatch != D::WorkspaceTooSmall ||
        device.services != before) return 7;
    ++checked;
    if (rs::Exchange::process(peer, large, output, workspace).written != 4120 ||
        get32(output, 24) != 1234 || get32(output, 4116) != 0 || workspace.used()) return 8;
    ++checked;
    for (unsigned source = 0; source <= 21; source += 3) {
        for (unsigned destination = 0; destination <= 21; destination += 3) {
            std::copy(valid.begin(), valid.end(), input.begin() + source);
            if (rs::Exchange::process(peer, Input{input}.subspan(source, 29),
                Output{input}.subspan(destination, 29), workspace).written != 29) return 9;
            if (get32(input, destination + 24) != 1234 || input[destination + 28] != std::byte{1}) return 10;
            ++checked;
        }
    }
    peer.reset();
    if (rs::Exchange::process(peer, valid, output, workspace).dispatch != D::NotReady) return 11;
    ++checked;
    if (rs::Bind::process(peer, view, descriptor.fingerprint() ^ 1, bindRequest, output).dispatch != D::NotReady || peer.ready()) return 12;
    ++checked;
    if (rs::Bind::process(peer, view, descriptor.fingerprint(), bindRequest, output).dispatch != D::Ok) return 13;
    ++checked;
    return 0;
}

__attribute__((noinline)) std::uint32_t window(unsigned profile, unsigned iterations)
{
    std::uint32_t sum = 0;
    const auto payload = Input{input}.subspan(24, 5);
    for (unsigned i = 0; i < iterations; ++i) {
        if (profile == 0) {
            const auto result = view.fields.writeEncoded(runtimeId, payload, workspace);
            sum += 24 + static_cast<unsigned>(result.dispatch);
        } else if (profile == 2) {
            const auto result = view.commands.executeEncoded(runtimeId, payload, workspace);
            sum += 24 + static_cast<unsigned>(result.dispatch);
        } else if (profile == 4) {
            const auto result = view.services.callEncoded(runtimeId, payload, Output{output}.subspan(24, 5), workspace);
            sum += 24 + result.written + static_cast<unsigned>(result.dispatch);
        } else if (profile == 7) {
            const auto result = rs::Bind::process(peer, view, descriptor.fingerprint(), bindRequest, output);
            sum += result.written + static_cast<unsigned>(result.dispatch);
        } else {
            const auto result = rs::Exchange::process(peer, Input{input}.first(29), output, workspace);
            sum += result.written + static_cast<unsigned>(result.dispatch);
        }
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
    bindRequest = handshake();
}

extern "C" void bench_loop()
{
    std::uint8_t command = 0;
    if (HAL_UART_Receive(&huart3, &command, 1, 100) != HAL_OK) return;
    if (command == 'P') { line("EXCHANGE IDLE 1\r\n"); return; }
    if (command != 'R') return;
    char report[128];
    std::snprintf(report, sizeof(report), "EXCHANGE READY %lu 24 32\r\n", static_cast<unsigned long>(SystemCoreClock));
    line(report);
    const auto valid = correctness();
    std::snprintf(report, sizeof(report), "EXCHANGE CHECK %d %u\r\n", valid, checked); line(report);
    if (valid != 0 || SystemCoreClock != 600000000u) return;
    for (unsigned profile = 0; profile < 8; ++profile) {
        const auto packet = fixture::packet(profile < 2 ? Op::FieldWrite : profile < 4 ? Op::Command : Op::Service,
                                            profile == 6 ? 3 : 0, Config{1234, true});
        std::copy(packet.begin(), packet.end(), input.begin());
        const auto iterations = profile == 6 ? 256u : 4096u;
        for (unsigned rep = 0; rep < 5; ++rep) {
            SCB_CleanInvalidateDCache(); SCB_InvalidateICache(); __DSB(); __ISB();
            (void)window(profile, 32);
            const auto mask = __get_PRIMASK(); __disable_irq(); __DSB(); __ISB();
            const auto start = DWT->CYCCNT;
            const auto sum = window(profile, iterations);
            const auto elapsed = DWT->CYCCNT - start;
            __set_PRIMASK(mask);
            std::snprintf(report, sizeof(report), "EXCHANGE T %u %u %u %lu %lu\r\n", profile, rep, iterations,
                static_cast<unsigned long>(elapsed), static_cast<unsigned long>(sum));
            line(report);
        }
    }
    line("EXCHANGE DONE\r\n");
}
