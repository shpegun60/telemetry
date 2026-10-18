/*
 * @file Probe.cpp
 * @brief Compile the actual optional mailbox into the bounded UART fixture.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#include "uart_bench.h"
#include <integration/telemetry/v3/RpcMailbox.cpp>

extern "C" {
BenchCounter g_bench_usart_irq{};
BenchCounter g_bench_tx_dma_irq{};
BenchCounter g_bench_rx_dma_irq{};
}
