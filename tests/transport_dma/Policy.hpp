/* Byte-only fixture admission policy; typed DTOs are not part of this port. MIT. */
#ifndef TELEMETRY_TESTS_TRANSPORT_DMA_POLICY_HPP
#define TELEMETRY_TESTS_TRANSPORT_DMA_POLICY_HPP
#pragma once
#include <client/DeviceClient.hpp>
#include <array>

namespace transport_dma_fixture {
struct Policy {
	inline static std::array<std::byte, 64> observedHeader{};
	static constexpr std::size_t scratchBytes = 1, scratchAlign = 1;

	static telemetry::client::Input descriptorHeader() noexcept
	{
		return observedHeader;
	}
};

using Client = telemetry::client::DeviceClient<Policy, 4, 256, 64>;
} // namespace transport_dma_fixture
#endif // TELEMETRY_TESTS_TRANSPORT_DMA_POLICY_HPP
