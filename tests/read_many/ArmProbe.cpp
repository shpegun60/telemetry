/* Selected roots only: linked sections are not whole-firmware Flash. MIT. */
#include "../../examples/read_many/Model.hpp"
#include <integration/encoded/Dispatcher.hpp>
static std::array<std::byte, 64> scratch{};

extern "C" std::size_t read_many_probe(const std::byte* request, std::size_t requestSize,
                                       std::byte* output, std::size_t capacity) noexcept
{
	telemetry::Workspace workspace{scratch};
#if defined(READ_MANY_LEGACY_PROBE)
	return telemetry::integration::encoded::dispatchEncoded(
	           peer::model.view(), workspace, 1, {request, requestSize}, {output, capacity})
	    .written;
#else
	const telemetry::protocol::encoded::read_many::Capability capability{
	    1, peer::descriptor.fingerprint()};
	return telemetry::integration::encoded::dispatchEncoded(peer::model.view(), workspace, 1,
	                                                        {request, requestSize},
	                                                        {output, capacity}, capability)
	    .written;
#endif
}
