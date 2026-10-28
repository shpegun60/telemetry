/*
 * @file DemoPeer.hpp
 * @brief Opaque native demonstration peer for the runtime Qt Inspector.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#ifndef TELEMETRY_EXAMPLE_INSPECTOR_QML_DEMO_PEER_HPP
#define TELEMETRY_EXAMPLE_INSPECTOR_QML_DEMO_PEER_HPP
#pragma once

#include <client/ClientResult.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>

namespace inspector_qml {

// Only the peer implementation knows native DTOs. The controller consumes the
// immutable Descriptor and complete Encoded frames through this boundary.
class DemoPeer {
public:
	DemoPeer();
	~DemoPeer();
	DemoPeer(const DemoPeer&) = delete;
	DemoPeer& operator=(const DemoPeer&) = delete;

	[[nodiscard]] telemetry::client::Input descriptor() const noexcept;
	// A valid request produces a correlated reply, including Model refusals.
	// Envelope, session, overlap or capacity refusal returns zero bytes.
	[[nodiscard]] std::size_t dispatch(telemetry::client::Input request,
	                                   telemetry::client::Output reply,
	                                   std::uint32_t session) noexcept;

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};

} // namespace inspector_qml
#endif // TELEMETRY_EXAMPLE_INSPECTOR_QML_DEMO_PEER_HPP
