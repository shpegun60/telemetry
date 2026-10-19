/* Test-owned UART TX copy with explicit driver release. MIT. */
#ifndef TELEMETRY_TESTS_TRANSPORT_DMA_TXSTORAGE_HPP
#define TELEMETRY_TESTS_TRANSPORT_DMA_TXSTORAGE_HPP
#pragma once
#include <client/Backend.hpp>
#include <algorithm>
#include <array>

namespace transport_dma_fixture {
// Owner-only state. An ISR publishes completion to the owner; cancellation of
// the logical request is deliberately absent from this buffer's release API.
class TxStorage {
public:
	static constexpr std::size_t capacity = 128;
	enum class State {
		Idle,
		Copied,
		DriverBorrow,
		AbortPending
	};

	[[nodiscard]] bool copy(telemetry::client::Packet packet) noexcept
	{
		if (state_ != State::Idle || !packet || packet.bytes.size() > bytes_.size() ||
		    serial_ == UINT32_MAX)
			return false;
		std::copy(packet.bytes.begin(), packet.bytes.end(), bytes_.begin());
		packet_ = packet;
		packet_.bytes = {bytes_.data(), packet.bytes.size()};
		++serial_;
		state_ = State::Copied;
		return true;
	}

	[[nodiscard]] telemetry::client::Packet packet() const noexcept
	{
		return packet_;
	}

	[[nodiscard]] State state() const noexcept
	{
		return state_;
	}

	[[nodiscard]] std::uint32_t lease() const noexcept
	{
		return serial_;
	}

	[[nodiscard]] bool driverBorrow() noexcept
	{
		if (state_ != State::Copied)
			return false;
		state_ = State::DriverBorrow;
		return true;
	}

	[[nodiscard]] bool requestAbort() noexcept
	{
		if (state_ != State::DriverBorrow)
			return false;
		state_ = State::AbortPending;
		return true;
	}

	[[nodiscard]] bool discardUnsent() noexcept
	{
		if (state_ != State::Copied)
			return false;
		clear();
		return true;
	}

	[[nodiscard]] bool driverComplete(std::uint32_t lease) noexcept
	{
		// Abort completion is a separate HAL quiescence boundary. A concurrent
		// normal completion must not allow reuse while its abort callback is due.
		if (lease != serial_ || state_ != State::DriverBorrow)
			return false;
		clear();
		return true;
	}

	[[nodiscard]] bool abortComplete(std::uint32_t lease) noexcept
	{
		if (lease != serial_ || state_ != State::AbortPending)
			return false;
		clear();
		return true;
	}

private:
	void clear() noexcept
	{
		state_ = State::Idle;
		packet_ = {};
	}

	alignas(32) std::array<std::byte, capacity> bytes_{};
	telemetry::client::Packet packet_{};
	State state_ = State::Idle;
	std::uint32_t serial_ = 0;
};
} // namespace transport_dma_fixture
#endif // TELEMETRY_TESTS_TRANSPORT_DMA_TXSTORAGE_HPP
