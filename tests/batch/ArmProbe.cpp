/* Linked selected roots, not whole-firmware Flash or peak stack. MIT. */
#include "../../examples/read_many/Model.hpp"
#include <integration/encoded/Dispatcher.hpp>
#ifndef BATCH_LEGACY_PROBE
#include <integration/encoded/BatchDispatcher.hpp>
#endif
static std::array<std::byte, 1024> scratch{};

extern "C" std::size_t batch_probe(const std::byte* request, std::size_t size, std::byte* output,
                                   std::size_t capacity) noexcept
{
	telemetry::Workspace workspace{scratch};
#ifdef BATCH_LEGACY_PROBE
	return telemetry::integration::encoded::dispatchEncoded(peer::model.view(), workspace, 1,
	                                                        {request, size}, {output, capacity})
	    .written;
#else
	const telemetry::protocol::encoded::batch::Capability capability{
	    1, peer::descriptor.fingerprint()};
	return telemetry::integration::encoded::dispatchEncoded(
	           peer::model.view(), workspace, 1, {request, size}, {output, capacity}, capability)
	    .written;
#endif
}
