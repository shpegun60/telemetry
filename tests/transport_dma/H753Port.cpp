/* H753 DMA ownership: actual HAL compile-only, host execution with a HAL double. MIT. */
#include "H753Port.hpp"

namespace transport_dma_fixture {
namespace tc = telemetry::client;

namespace {
constexpr std::uint32_t rxDone = 1, rxAborted = 2, driverError = 4;
}

H753Port::Memory H753Port::cachedMemory(bool (*accessible)(const void*,
                                                           std::size_t) noexcept) noexcept
{
	return {accessible,
	        +[](void* bytes, std::size_t size) noexcept {
		        SCB_CleanDCache_by_Addr(static_cast<std::uint32_t*>(bytes), size);
		        __DSB();
	        },
	        +[](void* bytes, std::size_t size) noexcept {
		        SCB_CleanInvalidateDCache_by_Addr(static_cast<std::uint32_t*>(bytes), size);
		        __DSB();
	        },
	        +[](void* bytes, std::size_t size) noexcept {
		        SCB_InvalidateDCache_by_Addr(static_cast<std::uint32_t*>(bytes), size);
		        __DSB();
	        }};
}

H753Port::H753Port(UART_HandleTypeDef& uart, Client& client, tc::Backend& backend,
                   Memory memory) noexcept
    : uart_(uart), client_(client), backend_(backend), memory_(memory)
{
	if (!validConfiguration(uart) || !memory.accessible || !memory.cleanTx || !memory.prepareRx ||
	    !memory.completeRx)
		std::abort();
}

bool H753Port::validConfiguration(const UART_HandleTypeDef& uart) noexcept
{
	if (!uart.hdmatx || !uart.hdmarx || uart.Init.WordLength != UART_WORDLENGTH_8B ||
	    uart.Init.Parity != UART_PARITY_NONE || uart.Init.Mode != UART_MODE_TX_RX)
		return false;
	const auto bytes = [](const DMA_HandleTypeDef& dma, std::uint32_t direction) noexcept {
		return dma.Init.Mode == DMA_NORMAL && dma.Init.Direction == direction &&
		       dma.Init.PeriphInc == DMA_PINC_DISABLE && dma.Init.MemInc == DMA_MINC_ENABLE &&
		       dma.Init.PeriphDataAlignment == DMA_PDATAALIGN_BYTE &&
		       dma.Init.MemDataAlignment == DMA_MDATAALIGN_BYTE;
	};
	return bytes(*uart.hdmatx, DMA_MEMORY_TO_PERIPH) && bytes(*uart.hdmarx, DMA_PERIPH_TO_MEMORY);
}

bool H753Port::send(tc::Packet packet) noexcept
{
	if (events_.load(std::memory_order_acquire) || completed_.load(std::memory_order_acquire) ||
	    aborted_.load(std::memory_order_acquire))
		return false;
	if (!tx_.copy(packet))
		return false;
	const auto copy = tx_.packet();
	if (!memory_.accessible(copy.bytes.data(), TxStorage::capacity)) {
		(void)tx_.discardUnsent();
		backend_.cancel(tc::Status::TransportError);
		return false;
	}
	if (!client_.prepareHandoff(copy.session, copy.requestId)) {
		(void)tx_.discardUnsent();
		return false;
	}
	if (!tx_.driverBorrow())
		return false;
	lease_.store(tx_.lease(), std::memory_order_release);
	memory_.cleanTx(const_cast<std::byte*>(copy.bytes.data()), TxStorage::capacity);
	if (HAL_UART_Transmit_DMA(
	        &uart_, reinterpret_cast<std::uint8_t*>(const_cast<std::byte*>(copy.bytes.data())),
	        static_cast<std::uint16_t>(copy.bytes.size())) != HAL_OK) {
		backend_.cancel(tc::Status::TransportError);
		requestAbort(); // Error alone does not establish driver release.
		return false;
	}
	(void)client_.onSendAccepted(copy.session, copy.requestId);
	return true;
}

bool H753Port::receive(std::size_t bytes, std::uint32_t session, std::uint32_t request) noexcept
{
	if (rxBusy_ || rxAborting_ || events_.load(std::memory_order_acquire) || bytes == 0 ||
	    bytes > rx_.size() || !memory_.accessible(rx_.data(), rx_.size()))
		return false;
	rxBytes_ = bytes;
	rxSession_ = session;
	rxRequest_ = request;
	rxBusy_ = true;
	memory_.prepareRx(rx_.data(), rx_.size());
	if (HAL_UART_Receive_DMA(&uart_, reinterpret_cast<std::uint8_t*>(rx_.data()),
	                         static_cast<std::uint16_t>(bytes)) != HAL_OK) {
		backend_.cancel(tc::Status::TransportError);
		requestAbort();
		return false;
	}
	return true;
}

void H753Port::poll(std::uint32_t now) noexcept
{
	const auto complete = completed_.exchange(0, std::memory_order_acquire);
	const auto abort = aborted_.exchange(0, std::memory_order_acquire);
	if (complete)
		(void)tx_.driverComplete(complete);
	if (abort)
		(void)tx_.abortComplete(abort);
	const auto events = events_.exchange(0, std::memory_order_acquire);
	if (events & driverError) {
		backend_.cancel(tc::Status::TransportError);
		requestAbort(); // UART error callback never releases TX bytes.
	}
	if (events & rxAborted) {
		rxBusy_ = false;
		rxAborting_ = false;
	}
	if ((events & rxDone) && rxBusy_ && !rxAborting_) {
		memory_.completeRx(rx_.data(), rx_.size());
		if (!(events & driverError))
			(void)client_.onReply(tc::Input{rx_}.first(rxBytes_), rxSession_, rxRequest_);
		rxBusy_ = false;
	}
	client_.process(now);
}

void H753Port::requestAbort() noexcept
{
	if (tx_.requestAbort())
		(void)HAL_UART_AbortTransmit_IT(&uart_);
	if (rxBusy_ && !rxAborting_) {
		rxAborting_ = true;
		(void)HAL_UART_AbortReceive_IT(&uart_);
	}
	// A failed abort leaves this fixture quarantined until real completion.
}

bool H753Port::driverIdle() const noexcept
{
	return tx_.state() == TxStorage::State::Idle && !rxBusy_ && !rxAborting_ &&
	       !events_.load(std::memory_order_acquire) &&
	       !completed_.load(std::memory_order_acquire) && !aborted_.load(std::memory_order_acquire);
}

void H753Port::txCompleteIsr(UART_HandleTypeDef* uart) noexcept
{
	if (uart == &uart_)
		completed_.store(lease_.load(std::memory_order_acquire), std::memory_order_release);
}

void H753Port::txAbortCompleteIsr(UART_HandleTypeDef* uart) noexcept
{
	if (uart == &uart_)
		aborted_.store(lease_.load(std::memory_order_acquire), std::memory_order_release);
}

void H753Port::rxCompleteIsr(UART_HandleTypeDef* uart) noexcept
{
	if (uart == &uart_)
		events_.fetch_or(rxDone, std::memory_order_release);
}

void H753Port::rxAbortCompleteIsr(UART_HandleTypeDef* uart) noexcept
{
	if (uart == &uart_)
		events_.fetch_or(rxAborted, std::memory_order_release);
}

void H753Port::errorIsr(UART_HandleTypeDef* uart) noexcept
{
	if (uart == &uart_)
		events_.fetch_or(driverError, std::memory_order_release);
}
} // namespace transport_dma_fixture
