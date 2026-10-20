/* Host execution of the actual H753Port implementation against a HAL double. MIT. */
#include "H753Port.hpp"
#include <integration/rpc/Backend.hpp>
#include <integration/encoded/RetryingBackend.hpp>
#include <telemetry/descriptor/Descriptor.hpp>
#include "../structured/rpc/Fixture.hpp"
#include <cstdio>

namespace tc = telemetry::client;
namespace fixture = transport_dma_fixture;
static unsigned checks = 0, failures = 0;

static void check(bool value, unsigned line) noexcept
{
	++checks;
	if (!value) {
		++failures;
		std::fprintf(stderr, "transport_dma_hal line=%u failed\n", line);
	}
}

#define CHECK(value) check(bool(value), __LINE__)

struct Harness {
	std::uint32_t now = 0;
	std::array<std::byte, 256> request{}, result{};
	std::array<std::byte, 40> packet{};
	tc::rpc::RpcClient native{0, request, result, packet, {3, 80, 1, 2}};
	tc::Clock clock{this, +[](void* p) noexcept {
		                return static_cast<Harness*>(p)->now;
	                }};
	telemetry::integration::rpc::RpcBackend rpcBackend{native, clock};
	std::array<std::byte, 128> retryTx{}, retryResult{};
	telemetry::integration::encoded::RetryingEncodedBackend retryBackend{
	    retryTx, retryResult, clock, {.replyTimeoutMs = 3, .maxRetries = 2}};
	tc::Backend& backend;
	fixture::Client client{backend, clock};
	DMA_HandleTypeDef tx{}, rx{[] {
		DMA_InitTypeDef init{};
		init.Direction = DMA_PERIPH_TO_MEMORY;
		return init;
	}()};
	UART_HandleTypeDef uart{&tx, &rx}, foreign{&tx, &rx};
	fixture::H753Port port{
	    uart, client, backend,
	    fixture::H753Port::cachedMemory(+[](const void* p, std::size_t n) noexcept {
		    return reinterpret_cast<std::uintptr_t>(p) % 32 == 0 && n == 128;
	    })};

	explicit Harness(bool retry = false)
	    : backend(retry ? static_cast<tc::Backend&>(retryBackend)
	                    : static_cast<tc::Backend&>(rpcBackend))
	{
		hal_double::state = {};
		const telemetry::descriptor::Descriptor descriptor{rpc_fixture::model};
		CHECK(descriptor.read(0, fixture::Policy::observedHeader).written == 64);
		CHECK(client.admitDescriptor(fixture::Policy::observedHeader, 1) == tc::Status::Ok);
	}

	~Harness()
	{
		client.close();
		for (unsigned i = 0; i < 12 && !client.drained(); ++i)
			port.poll(++now);
		CHECK(client.drained());
		port.requestAbort();
		port.txAbortCompleteIsr(&uart);
		port.rxAbortCompleteIsr(&uart);
		port.poll(now);
		CHECK(port.driverIdle());
	}

	tc::Submission command()
	{
		return client.submitEncoded(
		    {tc::Operation::Command, 1, {}, 0},
		    +[](const tc::ResultView&) noexcept {
		    },
		    12);
	}

	void send()
	{
		CHECK(command());
		CHECK(port.send(client.nextPacket()));
		CHECK(hal_double::state.sends == 1 && hal_double::state.cleans == 1);
	}
};

static void completionBeforeAbort()
{
	Harness h;
	h.send();
	const auto original = hal_double::state.tx;
	std::array<std::uint8_t, 128> saved{};
	std::copy_n(original, hal_double::state.txBytes, saved.begin());
	h.port.requestAbort();
	CHECK(hal_double::state.txAborts == 1);
	h.port.txCompleteIsr(&h.uart); // Normal completion races the abort callback.
	h.port.poll(h.now);
	CHECK(!h.port.driverIdle());
	CHECK(!h.port.send({tc::Input{reinterpret_cast<const std::byte*>(saved.data()), 8}, 2, 2}));
	CHECK(std::equal(saved.begin(), saved.begin() + hal_double::state.txBytes, original));
	h.port.txAbortCompleteIsr(&h.foreign);
	h.port.poll(h.now);
	CHECK(!h.port.driverIdle());
	h.port.txAbortCompleteIsr(&h.uart);
	h.port.poll(h.now);
	CHECK(h.port.driverIdle());
}

static void receiveAbortOrdering()
{
	Harness h;
	CHECK(h.port.receive(14, 1, 1));
	CHECK(hal_double::state.preparations == 1);
	h.port.requestAbort();
	CHECK(hal_double::state.rxAborts == 1);
	h.port.rxCompleteIsr(&h.uart);
	h.port.poll(h.now);
	CHECK(!h.port.driverIdle());
	CHECK(!h.port.receive(14, 2, 2));
	CHECK(hal_double::state.receives == 1 && hal_double::state.invalidations == 0);
	h.port.rxAbortCompleteIsr(&h.foreign);
	h.port.poll(h.now);
	CHECK(!h.port.driverIdle());
	h.port.rxAbortCompleteIsr(&h.uart);
	CHECK(!h.port.receive(14, 2, 2)); // Callback has not been consumed by the owner.
	h.port.poll(h.now);
	CHECK(h.port.driverIdle());
	CHECK(h.port.receive(14, 2, 2));
	h.port.rxCompleteIsr(&h.uart);
	CHECK(!h.port.receive(14, 3, 3));
	h.port.poll(h.now);
	CHECK(h.port.driverIdle() && hal_double::state.invalidations == 1);
}

static void errorDoesNotRelease()
{
	Harness h;
	h.send();
	h.port.errorIsr(&h.foreign);
	h.port.poll(h.now);
	CHECK(hal_double::state.txAborts == 0 && !h.port.driverIdle());
	h.port.errorIsr(&h.uart);
	h.port.poll(h.now);
	CHECK(hal_double::state.txAborts == 1 && !h.port.driverIdle());
	CHECK(h.client.recoveryRequired());
	h.port.txCompleteIsr(&h.uart);
	h.port.poll(h.now);
	CHECK(!h.port.driverIdle());
	h.port.txAbortCompleteIsr(&h.uart);
	h.port.poll(h.now);
	CHECK(h.port.driverIdle());
}

static void failedAbortStaysQuarantined()
{
	Harness h;
	h.send();
	hal_double::state.abortResult = HAL_ERROR;
	h.port.requestAbort();
	h.port.txCompleteIsr(&h.uart);
	h.port.poll(h.now);
	CHECK(!h.port.driverIdle()); // Function return/error is not a release event.
	hal_double::state.abortResult = HAL_OK;
	h.port.txAbortCompleteIsr(&h.uart); // Explicit test driver confirms quiescence.
	h.port.poll(h.now);
	CHECK(h.port.driverIdle());
}

static void timeoutKeepsBytes()
{
	Harness h;
	h.send();
	const auto original = hal_double::state.tx;
	std::array<std::uint8_t, 128> saved{};
	std::copy_n(original, hal_double::state.txBytes, saved.begin());
	h.now = 12;
	h.port.poll(h.now);
	CHECK(h.client.recoveryRequired());
	CHECK(!h.port.driverIdle());
	CHECK(std::equal(saved.begin(), saved.begin() + hal_double::state.txBytes, original));
	h.port.requestAbort();
	h.port.txAbortCompleteIsr(&h.uart);
	h.port.poll(h.now);
	CHECK(h.port.driverIdle());
}

static void configuration()
{
	DMA_HandleTypeDef tx{}, rx{};
	rx.Init.Direction = DMA_PERIPH_TO_MEMORY;
	UART_HandleTypeDef uart{&tx, &rx};
	CHECK(fixture::H753Port::validConfiguration(uart));
	for (auto* member : {&uart.Init.WordLength, &uart.Init.Parity, &uart.Init.Mode}) {
		const auto saved = *member;
		*member = 99;
		CHECK(!fixture::H753Port::validConfiguration(uart));
		*member = saved;
	}
	for (auto* dma : {&tx, &rx}) {
		for (auto* member :
		     {&dma->Init.Mode, &dma->Init.Direction, &dma->Init.PeriphInc, &dma->Init.MemInc,
		      &dma->Init.PeriphDataAlignment, &dma->Init.MemDataAlignment}) {
			const auto saved = *member;
			*member = 99;
			CHECK(!fixture::H753Port::validConfiguration(uart));
			*member = saved;
		}
	}
	uart.hdmatx = nullptr;
	CHECK(!fixture::H753Port::validConfiguration(uart));
	uart.hdmatx = &tx;
	uart.hdmarx = nullptr;
	CHECK(!fixture::H753Port::validConfiguration(uart));
}

static void retryWaitsForDriver(bool expired, bool cancelled)
{
	Harness h(true);
	const auto submitted = h.command();
	CHECK(submitted);
	const auto first = h.client.nextPacket();
	CHECK(h.port.send(first));
	std::array<std::uint8_t, 128> saved{};
	std::copy_n(hal_double::state.tx, hal_double::state.txBytes, saved.begin());
	h.now = 3;
	h.port.poll(h.now);
	const auto retry = h.client.nextPacket();
	CHECK(retry && retry.requestId == first.requestId && retry.session == first.session);
	CHECK(!h.port.send(retry));
	CHECK(hal_double::state.sends == 1 && !h.port.driverIdle());
	CHECK(
	    std::equal(saved.begin(), saved.begin() + hal_double::state.txBytes, hal_double::state.tx));
	h.port.txCompleteIsr(&h.uart);
	h.port.poll(h.now);
	CHECK(h.port.driverIdle());
	if (expired)
		h.now = 12;
	if (cancelled)
		CHECK(h.client.cancel(submitted.handle));
	if (expired || cancelled) {
		CHECK(!h.port.send(retry)); // Fresh gate after physical release still refuses stale work.
		CHECK(hal_double::state.sends == 1 && h.client.recoveryRequired());
		CHECK(h.port.driverIdle());
	} else {
		CHECK(h.port.send(retry));
		CHECK(hal_double::state.sends == 2);
		CHECK(std::equal(saved.begin(), saved.begin() + hal_double::state.txBytes,
		                 hal_double::state.tx));
	}
}

int main()
{
	completionBeforeAbort();
	receiveAbortOrdering();
	errorDoesNotRelease();
	failedAbortStaysQuarantined();
	timeoutKeepsBytes();
	configuration();
	retryWaitsForDriver(false, false);
	retryWaitsForDriver(true, false);
	retryWaitsForDriver(false, true);
	std::printf("transport_dma_hal checks=%u failures=%u\n", checks, failures);
	return failures ? 1 : 0;
}
