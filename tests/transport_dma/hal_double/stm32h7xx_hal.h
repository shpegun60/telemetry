/* Executable host HAL double only; never an STM32 SDK or physical DMA proof. MIT. */
#ifndef TELEMETRY_TESTS_TRANSPORT_DMA_HAL_DOUBLE_STM32H7XX_HAL_H
#define TELEMETRY_TESTS_TRANSPORT_DMA_HAL_DOUBLE_STM32H7XX_HAL_H
#pragma once
#include <cstddef>
#include <cstdint>

inline constexpr std::uint32_t DMA_NORMAL = 0, DMA_MEMORY_TO_PERIPH = 1, DMA_PERIPH_TO_MEMORY = 2,
                               DMA_PINC_DISABLE = 0, DMA_MINC_ENABLE = 1, DMA_PDATAALIGN_BYTE = 0,
                               DMA_MDATAALIGN_BYTE = 0, UART_WORDLENGTH_8B = 0,
                               UART_PARITY_NONE = 0, UART_MODE_TX_RX = 3;

struct DMA_InitTypeDef {
	std::uint32_t Mode = DMA_NORMAL, Direction = DMA_MEMORY_TO_PERIPH, PeriphInc = DMA_PINC_DISABLE,
	              MemInc = DMA_MINC_ENABLE, PeriphDataAlignment = DMA_PDATAALIGN_BYTE,
	              MemDataAlignment = DMA_MDATAALIGN_BYTE;
};

struct DMA_HandleTypeDef {
	DMA_InitTypeDef Init{};
};

struct UART_InitTypeDef {
	std::uint32_t WordLength = UART_WORDLENGTH_8B, Parity = UART_PARITY_NONE,
	              Mode = UART_MODE_TX_RX;
};

struct UART_HandleTypeDef {
	DMA_HandleTypeDef* hdmatx;
	DMA_HandleTypeDef* hdmarx;
	UART_InitTypeDef Init{};
};

enum HAL_StatusTypeDef {
	HAL_OK,
	HAL_ERROR
};

namespace hal_double {
struct State {
	std::uint8_t* tx = nullptr;
	std::uint8_t* rx = nullptr;
	std::size_t txBytes = 0, rxBytes = 0;
	unsigned sends = 0, receives = 0, txAborts = 0, rxAborts = 0;
	unsigned cleans = 0, preparations = 0, invalidations = 0;
	HAL_StatusTypeDef txResult = HAL_OK, rxResult = HAL_OK, abortResult = HAL_OK;
};

inline State state{};
} // namespace hal_double

inline HAL_StatusTypeDef HAL_UART_Transmit_DMA(UART_HandleTypeDef*, std::uint8_t* p,
                                               std::uint16_t n)
{
	auto& s = hal_double::state;
	s.tx = p;
	s.txBytes = n;
	++s.sends;
	return s.txResult;
}

inline HAL_StatusTypeDef HAL_UART_Receive_DMA(UART_HandleTypeDef*, std::uint8_t* p, std::uint16_t n)
{
	auto& s = hal_double::state;
	s.rx = p;
	s.rxBytes = n;
	++s.receives;
	return s.rxResult;
}

inline HAL_StatusTypeDef HAL_UART_AbortTransmit_IT(UART_HandleTypeDef*)
{
	++hal_double::state.txAborts;
	return hal_double::state.abortResult;
}

inline HAL_StatusTypeDef HAL_UART_AbortReceive_IT(UART_HandleTypeDef*)
{
	++hal_double::state.rxAborts;
	return hal_double::state.abortResult;
}

inline void SCB_CleanDCache_by_Addr(std::uint32_t*, std::int32_t)
{
	++hal_double::state.cleans;
}

inline void SCB_CleanInvalidateDCache_by_Addr(std::uint32_t*, std::int32_t)
{
	++hal_double::state.preparations;
}

inline void SCB_InvalidateDCache_by_Addr(std::uint32_t*, std::int32_t)
{
	++hal_double::state.invalidations;
}

inline void __DSB()
{}
#endif // TELEMETRY_TESTS_TRANSPORT_DMA_HAL_DOUBLE_STM32H7XX_HAL_H
