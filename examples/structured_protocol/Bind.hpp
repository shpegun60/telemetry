/*
 * @file Bind.hpp
 * @brief One-time version and descriptor-fingerprint agreement for one peer.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 *
 * Check version and schema identity once before admitting endpoint packets.
 * The transport supplies the stable ModelView and expected fingerprint;
 * a Ready state is published only together with a complete reply.
 */
#ifndef EXAMPLE_STRUCTURED_PROTOCOL_BIND_HPP
#define EXAMPLE_STRUCTURED_PROTOCOL_BIND_HPP
#pragma once

#include "Binding.hpp"

namespace example::structured_protocol {

inline constexpr std::uint32_t bindRequestBytes = 16;
inline constexpr std::uint32_t bindResponseBytes = 8;
enum class BindStatus : std::uint8_t {
	Ready = 0,
	SchemaMismatch = 1,
	UnsupportedVersion = 2,
	InvalidRequest = 3
};

// Public methods:
// - process(): Negotiate peer readiness.
class Bind {
public:
	// model must describe the same immutable schema as expectedFingerprint.
	// Keep the named ModelView alive until reset/disconnect; temporary views,
	// braces and conversion proxies cannot silently create borrowed state.
	// Every attempt clears old readiness, including a reply-capacity failure.
	// Serialize this call with Exchange and reset the peer at disconnect.
	template<class... Explicit, class View>
	    requires(sizeof...(Explicit) == 0 && (std::same_as<View, telemetry::ModelView&> ||
	                                          std::same_as<View, const telemetry::ModelView&>))
	[[nodiscard]] static PacketResult process(Binding& peer, View&& model,
	                                          std::uint64_t expectedFingerprint, Input request,
	                                          Output response) noexcept
	{
		return processImpl(peer, model, expectedFingerprint, request, response,
		                   detail::CurrentExchangeAbiTag{});
	}

private:
	[[nodiscard]] static PacketResult processImpl(Binding&, const telemetry::ModelView&,
	                                              std::uint64_t, Input, Output,
	                                              detail::CurrentExchangeAbiTag) noexcept;
};

} // namespace example::structured_protocol
#endif
