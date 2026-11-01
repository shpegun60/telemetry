/*
 * @file RetryingBackend.hpp
 * @brief Optional bounded TEQ1 retransmission without server deduplication.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 *
 * Each retry can execute the endpoint again. A checked response ends this
 * invocation; it cannot undo another copy already delivered to the peer.
 * All methods are owner-only. The port copies each packet before its fresh
 * prepareHandoff and retains that copy until physical TX release. No method
 * here establishes DMA release or performs transport I/O.
 */
#ifndef TELEMETRY_INTEGRATION_ENCODED_RETRYING_BACKEND_HPP
#define TELEMETRY_INTEGRATION_ENCODED_RETRYING_BACKEND_HPP
#pragma once

#include "Backend.hpp"

namespace telemetry::integration::encoded {

struct EncodedRetryOptions {
	std::uint32_t replyTimeoutMs = 100;
	// Additional full-packet sends after the initial send; zero disables retries.
	std::uint32_t maxRetries = 3;
};

class RetryingEncodedBackend final : public ::telemetry::client::Backend {
public:
	explicit RetryingEncodedBackend(::telemetry::client::Output txStorage,
	                                ::telemetry::client::Output resultStorage,
	                                ::telemetry::client::Clock clock,
	                                EncodedRetryOptions options = {},
	                                bool enableReadMany = false) noexcept
	    : inner_(txStorage, resultStorage, clock, ::telemetry::client::maxTimeoutMs,
	             enableReadMany),
	      clock_(clock), options_(options)
	{
		if (options_.replyTimeoutMs == 0 ||
		    options_.replyTimeoutMs > ::telemetry::client::maxTimeoutMs)
			std::abort();
	}

	RetryingEncodedBackend(const RetryingEncodedBackend&) = delete;
	RetryingEncodedBackend& operator=(const RetryingEncodedBackend&) = delete;
	RetryingEncodedBackend(RetryingEncodedBackend&&) = delete;
	RetryingEncodedBackend& operator=(RetryingEncodedBackend&&) = delete;

	[[nodiscard]] bool supportsReadMany() const noexcept
	{
		return inner_.supportsReadMany();
	}

	[[nodiscard]] ::telemetry::client::Status start(::telemetry::client::RequestView request,
	                                                std::uint32_t now) noexcept override
	{
		const auto status = inner_.start(request, now);
		if (status == ::telemetry::client::Status::Ok) {
			resetAttempt();
			packet_ = inner_.nextPacket();
			attempt_ = Attempt::Offered;
		}
		return status;
	}

	void process(std::uint32_t now) noexcept override
	{
		// The remaining enqueue budget is the only overall timeout. A per-reply
		// timeout must not reset that deadline or the inner invocation identity.
		inner_.process(now);
		if (inner_.completed()) {
			attempt_ = Attempt::Idle;
			return;
		}
		if (attempt_ != Attempt::Waiting || !::telemetry::client::expired(now, replyDeadline_))
			return;
		if (retries_ == options_.maxRetries) {
			inner_.cancel(::telemetry::client::Status::Timeout);
			attempt_ = Attempt::Idle;
		} else {
			attempt_ = Attempt::Offered;
		}
	}

	[[nodiscard]] bool completed() const noexcept override
	{
		return inner_.completed();
	}

	[[nodiscard]] ::telemetry::client::ResultView result() const noexcept override
	{
		return inner_.result();
	}

	void releaseResult() noexcept override
	{
		if (inner_.completed()) {
			inner_.releaseResult();
			resetAttempt();
		}
	}

	void cancel(::telemetry::client::Status reason) noexcept override
	{
		inner_.cancel(reason);
		if (inner_.completed())
			attempt_ = Attempt::Idle;
	}

	[[nodiscard]] bool ready() const noexcept override
	{
		return inner_.ready();
	}

	[[nodiscard]] bool recoveryRequired() const noexcept override
	{
		return inner_.recoveryRequired();
	}

	[[nodiscard]] std::uint32_t session() const noexcept override
	{
		return inner_.session();
	}

	[[nodiscard]] bool openSession(std::uint32_t generation) noexcept override
	{
		if (!inner_.openSession(generation))
			return false;
		resetAttempt();
		return true;
	}

	[[nodiscard]] bool confirmSessionClosed(std::uint32_t generation) noexcept override
	{
		return inner_.confirmSessionClosed(generation);
	}

	void close() noexcept override
	{
		inner_.close();
		attempt_ = Attempt::Idle;
	}

	[[nodiscard]] ::telemetry::client::Packet nextPacket() noexcept override
	{
		return attempt_ == Attempt::Offered ? packet_ : ::telemetry::client::Packet{};
	}

	[[nodiscard]] bool prepareHandoff(std::uint32_t generation,
	                                  std::uint32_t requestId) noexcept override
	{
		if (attempt_ != Attempt::Offered || !matches(generation, requestId))
			return false;
		inner_.process(clock_.now());
		if (inner_.completed()) {
			attempt_ = Attempt::Idle;
			return false;
		}
		if (!prepared_) {
			if (!inner_.prepareHandoff(generation, requestId))
				return false;
			prepared_ = true;
		}
		// The inner backend stays Prepared/Sent across retries: its sticky
		// possible-execution flag, exact frame, parser and recovery rules apply.
		attempt_ = Attempt::Prepared;
		return true;
	}

	[[nodiscard]] bool onSendAccepted(std::uint32_t generation,
	                                  std::uint32_t requestId) noexcept override
	{
		if (attempt_ != Attempt::Prepared || !matches(generation, requestId))
			return false;
		if (!accepted_) {
			if (!inner_.onSendAccepted(generation, requestId))
				return false;
		} else {
			inner_.process(clock_.now());
		}
		if (inner_.completed()) {
			attempt_ = Attempt::Idle;
			return true;
		}
		if (accepted_)
			++retries_; // Offered only while retries_ < maxRetries; no +1 overflow.
		accepted_ = true;
		replyDeadline_ = clock_.now() + options_.replyTimeoutMs;
		attempt_ = Attempt::Waiting;
		return true;
	}

	[[nodiscard]] bool onReply(::telemetry::client::Input bytes, std::uint32_t generation,
	                           std::uint32_t requestId) noexcept override
	{
		// A checked response from any earlier attempt can arrive while a retry
		// is Offered/Prepared. Never re-start the inner backend to retransmit.
		const bool consumed = inner_.onReply(bytes, generation, requestId);
		if (inner_.completed())
			attempt_ = Attempt::Idle;
		return consumed;
	}

private:
	enum class Attempt : std::uint8_t {
		Idle,
		Offered,
		Prepared,
		Waiting
	};

	[[nodiscard]] bool matches(std::uint32_t generation, std::uint32_t requestId) const noexcept
	{
		return generation == packet_.session && requestId == packet_.requestId;
	}

	void resetAttempt() noexcept
	{
		packet_ = {};
		retries_ = replyDeadline_ = 0;
		prepared_ = accepted_ = false;
		attempt_ = Attempt::Idle;
	}

	EncodedBackend inner_;
	::telemetry::client::Clock clock_;
	EncodedRetryOptions options_;
	::telemetry::client::Packet packet_{};
	std::uint32_t retries_ = 0, replyDeadline_ = 0;
	Attempt attempt_ = Attempt::Idle;
	bool prepared_ = false, accepted_ = false;
};

} // namespace telemetry::integration::encoded
#endif // TELEMETRY_INTEGRATION_ENCODED_RETRYING_BACKEND_HPP
