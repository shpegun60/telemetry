/* Bare-metal COBS/Resource/TEQ1 composition. SPDX-License-Identifier: MIT */
#define UART_ENGINE_IMPLEMENT
#include "uart/Uart.h"
#include "adapters/cobs/UartAdapter.h"
#include "cobs/Cobs.h"
#include "../inspector_serial/SerialProfile.hpp"
#include "ServerLoop.hpp"

struct Diagnostics {
	std::uint32_t magic, version, loops, rxPackets, replies, refused;
	std::uint32_t txFailures, uartRxOverrun, uartRxErrors, uartTxErrors, uartRestarts;
};

extern "C" {
volatile Diagnostics telemetry_h7s_diagnostics{0x54484e37, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0};
}

namespace {
using Link = cobs::Endpoint<wire::Pool<4, 1>, inspector_serial::FrameFormat>;
/* Ordinary .bss = AXI SRAM 0x24000000. Keep the complete Uart and COBS pool
   here: they contain the DMA buffers and must never be moved to DTCM. */
Uart<256, 8> serial;
Link link;
cobs::UartAdapter adapter{serial, link};
nucleo_demo::ServerLoop server{serial, link};
volatile bool txFailed = false;
} // namespace

int main()
{
	board_init();
	serial.setTxHandler([](bool ok) noexcept {
		if (!ok)
			txFailed = true;
	});
	if (!serial.init(&huart3) || !adapter.bind())
		Error_Handler();
	for (;;) {
		const auto now = HAL_GetTick();
		adapter.proceed(now); // RX handlers run here; CRC/COBS never runs in ISR.
		telemetry_h7s_diagnostics.loops = telemetry_h7s_diagnostics.loops + 1;
		if (txFailed) {
			txFailed = false;
			telemetry_h7s_diagnostics.txFailures = telemetry_h7s_diagnostics.txFailures + 1;
			// A failed TX is never replayed. Uart retains its lease until an
			// actual stop; endpoint.poll() then reclaims the encoded block.
		}
		const auto action = server.step(now);
		if (action == nucleo_demo::Action::Capacity)
			Error_Handler();
		if (action == nucleo_demo::Action::Dispatched || action == nucleo_demo::Action::Refused)
			telemetry_h7s_diagnostics.rxPackets = telemetry_h7s_diagnostics.rxPackets + 1;
		if (action == nucleo_demo::Action::Sent)
			telemetry_h7s_diagnostics.replies = telemetry_h7s_diagnostics.replies + 1;
		if (action == nucleo_demo::Action::Refused)
			telemetry_h7s_diagnostics.refused = telemetry_h7s_diagnostics.refused + 1;
		if (action == nucleo_demo::Action::TxFailed)
			telemetry_h7s_diagnostics.txFailures = telemetry_h7s_diagnostics.txFailures + 1;
		const auto stats = serial.stats();
		telemetry_h7s_diagnostics.uartRxOverrun = stats.rx_overrun;
		telemetry_h7s_diagnostics.uartRxErrors = stats.rx_errors;
		telemetry_h7s_diagnostics.uartTxErrors = stats.tx_errors;
		telemetry_h7s_diagnostics.uartRestarts = stats.restarts;
	}
}
