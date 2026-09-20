// Compare natural/padded command strides on the same H7S executable fixture.
// Authors: Ruslan Kovtun (shpegun60), codexAi. License: MIT.
#include "Telemetry.h"
#include "usart.h"
#include "uart_bench.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <new>
using namespace telemetry;
BenchCounter g_bench_usart_irq{},g_bench_rx_dma_irq{},g_bench_tx_dma_irq{};
volatile std::uint32_t strideSink=0;
struct StrideOwner {
    std::uint32_t number=0;
    __attribute__((noinline)) CommandResult run(std::uint16_t add) noexcept {
        strideSink=strideSink+number+add;
        return CommandResult::Executed;
    }
};
StrideOwner strideOwners[1024];
namespace {
template<std::size_t... I> constexpr auto rows(std::index_sequence<I...>) noexcept {
    return std::array<Command,sizeof...(I)>{detail::materializeCommand<&StrideOwner::run>("x",strideOwners[I])...};
}
alignas(32) constexpr auto flashRows=rows(std::make_index_sequence<1024>{});
using Rows=std::remove_cv_t<decltype(flashRows)>;
alignas(32) unsigned char ramStorage[sizeof(Rows)];
Rows* ramRows=nullptr;
// Keep initialization generic so GCC does not emit a second literal table.
__attribute__((noinline, noipa)) Rows* copyRows(void* storage, const Rows& source) noexcept {
    return new(storage) Rows(source);
}
__attribute__((section(".dtcm_ids"))) std::uint16_t ids[1024];
constexpr unsigned iterations=32768,repetitions=9;
void line(const char* text) {
    if(HAL_UART_Transmit(&huart3,reinterpret_cast<const std::uint8_t*>(text),
                        static_cast<std::uint16_t>(std::strlen(text)),2000)!=HAL_OK) Error_Handler();
}
__attribute__((noinline)) CommandResult execute(const CommandIndex& index,CommandId id,const Scalar* args) noexcept {
    return index.execute(id,args,1);
}
__attribute__((noinline)) unsigned loop(const CommandIndex& index,const Scalar* args,unsigned count) noexcept {
    unsigned result=0;
    for(unsigned i=0;i<count;++i) result+=static_cast<unsigned>(execute(index,ids[i%1024],args));
    return result;
}
}
extern "C" void bench_init() {
    MPU_Region_InitTypeDef r{};
    HAL_MPU_Disable();r.Enable=MPU_REGION_ENABLE;r.Number=MPU_REGION_NUMBER1;
    r.BaseAddress=0x24000000;r.Size=MPU_REGION_SIZE_512KB;r.AccessPermission=MPU_REGION_FULL_ACCESS;
    r.TypeExtField=MPU_TEX_LEVEL1;r.IsCacheable=MPU_ACCESS_CACHEABLE;
    r.IsBufferable=MPU_ACCESS_BUFFERABLE;r.IsShareable=MPU_ACCESS_NOT_SHAREABLE;
    r.DisableExec=MPU_INSTRUCTION_ACCESS_DISABLE;HAL_MPU_ConfigRegion(&r);HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
    SCB_EnableICache();SCB_EnableDCache();
    CoreDebug->DEMCR|=CoreDebug_DEMCR_TRCENA_Msk;DWT->CYCCNT=0;DWT->CTRL|=DWT_CTRL_CYCCNTENA_Msk;
    for(unsigned i=0;i<1024;++i)strideOwners[i].number=i;
    ramRows=copyRows(ramStorage,flashRows);
    __DSB();__ISB();
}
extern "C" void bench_loop() {
    std::uint8_t input=0;
    if(HAL_UART_Receive(&huart3,&input,1,100)!=HAL_OK||input!='R')return;
    if(SystemCoreClock!=600000000u || (SCB->CCR&(SCB_CCR_IC_Msk|SCB_CCR_DC_Msk))!=(SCB_CCR_IC_Msk|SCB_CCR_DC_Msk)) {
        line("STRIDE FAIL\r\n");return;
    }
    char text[180];
    std::snprintf(text,sizeof text,"STRIDE READY %u %u %u %u %u\r\n",static_cast<unsigned>(SystemCoreClock),
                  unsigned(sizeof(Command)),iterations,repetitions,unsigned(sizeof(Scalar)));line(text);
    const Scalar args[]={std::uint16_t{1}};
    for(unsigned mem=0;mem<2;++mem)for(unsigned capacity:{128u,1024u})for(unsigned profile=0;profile<3;++profile) {
        std::uint32_t state=0x19a753;
        for(unsigned i=0;i<1024;++i) {
            state^=state<<13;state^=state>>17;state^=state<<5;
            ids[i]=profile==0?0:profile==1?i%capacity:state%capacity;
        }
        const CommandIndex index{mem?ramRows->data():flashRows.data(),capacity};
        (void)loop(index,args,1024);
        for(unsigned rep=0;rep<repetitions;++rep) {
            strideSink=0;const auto mask=__get_PRIMASK();__disable_irq();__DSB();__ISB();
            const auto start=DWT->CYCCNT;const auto result=loop(index,args,iterations);
            const auto cycles=DWT->CYCCNT-start;__set_PRIMASK(mask);
            std::snprintf(text,sizeof text,"STRIDE T %u %u %u %u %lu %lu %u\r\n",mem,capacity,profile,rep,
                          static_cast<unsigned long>(cycles),static_cast<unsigned long>(strideSink),result);line(text);
        }
    }
    line("STRIDE DONE\r\n");
}
