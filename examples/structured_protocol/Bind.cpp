/*
 * @file Bind.cpp
 * @brief Parse one handshake and publish Ready only with a complete response.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 *
 * Implement the example handshake without adding connection policy to the
 * core library. Capture and validate the request before writing the reply
 * so an application may reuse one buffer for both directions.
 */

#include "Bind.hpp"
#include "detail/ExchangeWire.hpp"

namespace example::structured_protocol {

PacketResult Bind::processImpl(Binding& peer, const telemetry::ModelView& model,
                               std::uint64_t fingerprint, Input request, Output response,
                               detail::CurrentExchangeAbiTag) noexcept
{
	using D = PacketStatus;
	// Every attempt is a new agreement. Failure must not preserve a previous
	// Ready, and the transport serializes Bind against active requests.
	peer.reset();
	if (response.size() < bindResponseBytes)
		return {D::BufferTooSmall};

	auto status = BindStatus::InvalidRequest;
	auto dispatch = D::InvalidRequest;
	if (request.size() == bindRequestBytes && detail::magic(request, "TSBN")) {
		if (detail::load<std::uint16_t>(request, 4) != protocolMajor ||
		    detail::load<std::uint16_t>(request, 6) != protocolMinor) {
			status = BindStatus::UnsupportedVersion;
			dispatch = D::UnsupportedVersion;
		} else if (detail::load<std::uint64_t>(request, 8) != fingerprint) {
			status = BindStatus::SchemaMismatch;
			dispatch = D::NotReady;
		} else {
			status = BindStatus::Ready;
			dispatch = D::Ok;
		}
	}

	// Parsing has finished, so an in-place handshake buffer is safe too.
	detail::magic(response, "TSBA");
	response[4] = static_cast<std::byte>(status);
	response[5] = response[6] = response[7] = std::byte{0};
	if (status == BindStatus::Ready)
		peer.model_ = &model;
	return {dispatch, bindResponseBytes};
}

} // namespace example::structured_protocol
