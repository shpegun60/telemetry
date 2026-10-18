/* Owner-controlled backend for queue lifecycle checks. Authors: Ruslan Kovtun (shpegun60), codexAi. MIT. */
#ifndef TELEMETRY_TESTS_ASYNC_CLIENT_FIXTURE_HPP
#define TELEMETRY_TESTS_ASYNC_CLIENT_FIXTURE_HPP
#pragma once
#include <client/ThreadSafeClient.hpp>
#include <atomic>
#include <cstring>

namespace async_fixture {
namespace cl = telemetry::client;
inline std::atomic<std::uint32_t> now{0};
inline cl::Clock clock{nullptr, +[](void*) noexcept {
	                       return now.load();
                       }};

struct Codec {
	template<class T>
	static constexpr std::size_t wireSize = [] {
		if constexpr (std::is_void_v<T>)
			return std::size_t{0};
		else
			return sizeof(T);
	}();
	static constexpr std::size_t scratchBytes = 64, scratchAlign = alignof(std::max_align_t);
	inline static std::array<std::byte, 64> header{};
	inline static std::atomic<bool> pauseEncode{false}, enteredEncode{false};

	static cl::Input descriptorHeader() noexcept
	{
		return header;
	}

	template<class T>
	static std::uint8_t encode(const T& value, cl::Output bytes) noexcept
	{
		if (pauseEncode.load()) {
			enteredEncode.store(true);
			while (pauseEncode.load()) {
			}
		}
		if (bytes.size() != sizeof(T))
			return 1;
		std::memcpy(bytes.data(), &value, sizeof(T));
		return 0;
	}

	template<class T>
	static std::uint8_t decode(cl::Input bytes, T& value) noexcept
	{
		if (bytes.size() != sizeof(T))
			return 1;
		std::memcpy(&value, bytes.data(), sizeof(T));
		return 0;
	}
};

struct Field {
	static constexpr unsigned category = 1;
	using Value = std::uint32_t;
	std::uint32_t id = 42;
	std::uint8_t capabilities = 3;

	bool readable() const noexcept
	{
		return capabilities & 1;
	}

	bool writable() const noexcept
	{
		return capabilities & 2;
	}
};

struct Command {
	static constexpr unsigned category = 2;
	using Request = std::uint32_t;
	using Response = void;
	std::uint32_t id = 43;
};

struct EmptyCommand {
	static constexpr unsigned category = 2;
	using Request = void;
	using Response = void;
	std::uint32_t id = 44;
};

struct Service {
	static constexpr unsigned category = 3;
	using Request = std::uint32_t;
	using Response = std::uint32_t;
	std::uint32_t id = 45;
};

class Backend : public cl::Backend {
public:
	cl::RequestView request{};
	std::array<std::byte, 64> snapshot{}, response{};
	cl::ResultView outcome{};
	std::uint32_t generation = 0, starts = 0, closeCalls = 0;
	bool opened = false, active = false, done = false, recovery = false, autoComplete = false;

	cl::Status start(cl::RequestView view, std::uint32_t) noexcept override
	{
		if (!opened)
			return cl::Status::NotAdmitted;
		if (active || done)
			return cl::Status::Busy;
		request = view;
		if (view.payload.size() > snapshot.size())
			return cl::Status::BufferTooSmall;
		std::copy(view.payload.begin(), view.payload.end(), snapshot.begin());
		request.payload = cl::Input{snapshot}.first(view.payload.size());
		++starts;
		active = true;
		return cl::Status::Ok;
	}

	void process(std::uint32_t) noexcept override
	{
		if (autoComplete && active)
			finish();
	}

	void finish(std::uint32_t value = 99, cl::Status status = cl::Status::Ok) noexcept
	{
		std::memcpy(response.data(), &value, sizeof(value));
		outcome = {status, request.operation};
		outcome.payload = cl::Input{response}.first(request.responseBytes);
		active = false;
		done = true;
	}

	bool completed() const noexcept override
	{
		return done;
	}

	cl::ResultView result() const noexcept override
	{
		return outcome;
	}

	void releaseResult() noexcept override
	{
		done = false;
	}

	void cancel(cl::Status reason) noexcept override
	{
		if (active)
			finish(0, reason);
	}

	bool ready() const noexcept override
	{
		return opened && !recovery;
	}

	bool recoveryRequired() const noexcept override
	{
		return recovery;
	}

	std::uint32_t session() const noexcept override
	{
		return generation;
	}

	bool openSession(std::uint32_t next) noexcept override
	{
		if (recovery || next <= generation)
			return false;
		generation = next;
		opened = true;
		return true;
	}

	bool confirmSessionClosed(std::uint32_t old) noexcept override
	{
		if (old != generation || !recovery)
			return false;
		recovery = false;
		opened = false;
		return true;
	}

	void close() noexcept override
	{
		++closeCalls;
		opened = false;
	}

	cl::Packet nextPacket() noexcept override
	{
		return {};
	}

	bool prepareHandoff(std::uint32_t, std::uint32_t) noexcept override
	{
		return false;
	}

	bool onSendAccepted(std::uint32_t, std::uint32_t) noexcept override
	{
		return false;
	}

	bool onReply(cl::Input, std::uint32_t, std::uint32_t) noexcept override
	{
		return false;
	}
};
} // namespace async_fixture
#endif // TELEMETRY_TESTS_ASYNC_CLIENT_FIXTURE_HPP
