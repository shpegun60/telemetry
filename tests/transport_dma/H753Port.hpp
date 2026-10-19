/* Test-only H753 UART DMA fixture; application owns UART configuration/framing. MIT. */
#ifndef TELEMETRY_TESTS_TRANSPORT_DMA_H753PORT_HPP
#define TELEMETRY_TESTS_TRANSPORT_DMA_H753PORT_HPP
#pragma once
#include "TxStorage.hpp"
#include "Policy.hpp"
#include <stm32h7xx_hal.h>
#include <atomic>

namespace transport_dma_fixture {
// Construct in DMA-accessible SRAM with 32-byte aligned isolated cache lines.
// Exclusive UART ownership; application forwards HAL callbacks only to this port.
// No pins, clocks, IRQ priorities, framing or peripheral instance are configured.
class H753Port {
public:
	struct Memory {
		bool (*accessible)(const void*, std::size_t) noexcept;
		void (*cleanTx)(void*, std::size_t) noexcept;
		void (*prepareRx)(void*, std::size_t) noexcept;
		void (*completeRx)(void*, std::size_t) noexcept;
	};

	// Select these CMSIS cache operations explicitly, together with the board's
	// DMA-accessible SRAM validator. A noncacheable MPU region needs its own hooks.
	[[nodiscard]] static Memory cachedMemory(bool (*accessible)(const void*,
	                                                            std::size_t) noexcept) noexcept;
	[[nodiscard]] static bool validConfiguration(const UART_HandleTypeDef& uart) noexcept;
	H753Port(UART_HandleTypeDef& uart, Client& client, telemetry::client::Backend& backend,
	         Memory memory) noexcept;
	[[nodiscard]] bool send(telemetry::client::Packet packet) noexcept;
	// Framing must already supply the exact complete Resource reply length.
	// UART idle/partial progress alone is never a complete frame.
	[[nodiscard]] bool receive(std::size_t bytes, std::uint32_t session,
	                           std::uint32_t request) noexcept;
	void poll(std::uint32_t now) noexcept;
	void requestAbort() noexcept;
	[[nodiscard]] bool driverIdle() const noexcept;
	void txCompleteIsr(UART_HandleTypeDef* uart) noexcept;
	void txAbortCompleteIsr(UART_HandleTypeDef* uart) noexcept;
	void rxCompleteIsr(UART_HandleTypeDef* uart) noexcept;
	void rxAbortCompleteIsr(UART_HandleTypeDef* uart) noexcept;
	void errorIsr(UART_HandleTypeDef* uart) noexcept;

private:
	static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
	UART_HandleTypeDef& uart_;
	Client& client_;
	telemetry::client::Backend& backend_;
	Memory memory_;
	TxStorage tx_;
	alignas(32) std::array<std::byte, TxStorage::capacity> rx_{};
	std::atomic<std::uint32_t> lease_{0}, completed_{0}, aborted_{0}, events_{0};
	std::size_t rxBytes_ = 0;
	std::uint32_t rxSession_ = 0, rxRequest_ = 0;
	bool rxBusy_ = false, rxAborting_ = false;
};
} // namespace transport_dma_fixture
#endif // TELEMETRY_TESTS_TRANSPORT_DMA_H753PORT_HPP
