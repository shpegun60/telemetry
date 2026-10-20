/* Portable ARM compile roots; no HAL, scheduler or physical DMA execution. MIT. */
#include "TxStorage.hpp"
#include "Policy.hpp"
#include <integration/rpc/Backend.hpp>
#include <integration/encoded/RetryingBackend.hpp>

extern "C" bool transport_dma_copy(transport_dma_fixture::TxStorage& tx,
                                   transport_dma_fixture::Client& client) noexcept
{
	const auto packet = client.nextPacket();
	if (!tx.copy(packet))
		return false;
	if (!client.prepareHandoff(packet.session, packet.requestId)) {
		(void)tx.discardUnsent();
		return false;
	}
	return tx.driverBorrow();
}

extern "C" bool transport_dma_release(transport_dma_fixture::TxStorage& tx, std::uint32_t lease,
                                      bool aborted) noexcept
{
	return aborted ? tx.abortComplete(lease) : tx.driverComplete(lease);
}

extern "C" void transport_dma_owner(transport_dma_fixture::Client& client,
                                    std::uint32_t now) noexcept
{
	client.process(now);
}

extern "C" telemetry::client::Status
transport_dma_rpc(telemetry::integration::rpc::RpcBackend& backend,
                  telemetry::client::RequestView request, std::uint32_t now) noexcept
{
	return backend.start(request, now);
}

extern "C" telemetry::client::Status
transport_dma_retry(telemetry::integration::encoded::RetryingEncodedBackend& backend,
                    telemetry::client::RequestView request, std::uint32_t now) noexcept
{
	return backend.start(request, now);
}
