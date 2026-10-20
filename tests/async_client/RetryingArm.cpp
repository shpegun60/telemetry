/* Standalone Cortex-M7 retry roots and ABI-size evidence, without Model or OS. MIT. */
#include <integration/encoded/RetryingBackend.hpp>

namespace ie = telemetry::integration::encoded;
namespace cl = telemetry::client;
extern "C" {
extern const unsigned char encoded_backend_abi[sizeof(ie::EncodedBackend)] = {};
extern const unsigned char retrying_backend_abi[sizeof(ie::RetryingEncodedBackend)] = {};

cl::Status retrying_arm_start(ie::RetryingEncodedBackend& backend, cl::RequestView request,
                              std::uint32_t now) noexcept
{
	return backend.start(request, now);
}

void retrying_arm_process(ie::RetryingEncodedBackend& backend, std::uint32_t now) noexcept
{
	backend.process(now);
}

bool retrying_arm_handoff(ie::RetryingEncodedBackend& backend, cl::Output port) noexcept
{
	const auto packet = backend.nextPacket();
	if (!packet || packet.bytes.size() > port.size())
		return false;
	std::memcpy(port.data(), packet.bytes.data(), packet.bytes.size());
	return backend.prepareHandoff(packet.session, packet.requestId) &&
	       backend.onSendAccepted(packet.session, packet.requestId);
}

bool retrying_arm_reply(ie::RetryingEncodedBackend& backend, cl::Input reply, std::uint32_t session,
                        std::uint32_t request) noexcept
{
	return backend.onReply(reply, session, request);
}

void retrying_arm_cancel(ie::RetryingEncodedBackend& backend) noexcept
{
	backend.cancel(cl::Status::Cancelled);
}
}
