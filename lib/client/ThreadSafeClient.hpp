/* Optional producer facade. Authors: Ruslan Kovtun (shpegun60), codexAi. MIT. */
#ifndef TELEMETRY_LIB_TELEMETRY_CLIENT_THREADSAFECLIENT_HPP
#define TELEMETRY_LIB_TELEMETRY_CLIENT_THREADSAFECLIENT_HPP
#pragma once
#include "DeviceClient.hpp"

namespace telemetry::client {
// Construct DeviceClient with a real Lock policy (e.g. std::mutex or RTOS mutex).
// This wrapper posts work and wakes its owner; it never processes I/O/callbacks.
// close(): stop admission; keep producers/client alive until producer calls have
// returned and the owner has processed until drained(). No blocking owner join.
template<class Client>
class ThreadSafeClient {
	static_assert(!std::is_same_v<typename Client::LockPolicy, NoLock>,
	              "ThreadSafeClient requires a synchronized DeviceClient Lock policy");
	Client& client_;
	void* context_;
	void (*wake_)(void*) noexcept;

	void wake() const noexcept
	{
		if (wake_)
			wake_(context_);
	}

public:
	explicit ThreadSafeClient(Client& client, void (*wake)(void*) noexcept = nullptr,
	                          void* context = nullptr) noexcept
	    : client_(client), context_(context), wake_(wake)
	{}

	template<class... Args>
	Submission read(Args&&... args) noexcept
	{
		auto r = client_.read(std::forward<Args>(args)...);
		if (r)
			wake();
		return r;
	}

	template<class... Args>
	Submission write(Args&&... args) noexcept
	{
		auto r = client_.write(std::forward<Args>(args)...);
		if (r)
			wake();
		return r;
	}

	template<class... Args>
	Submission command(Args&&... args) noexcept
	{
		auto r = client_.command(std::forward<Args>(args)...);
		if (r)
			wake();
		return r;
	}

	template<class... Args>
	Submission service(Args&&... args) noexcept
	{
		auto r = client_.service(std::forward<Args>(args)...);
		if (r)
			wake();
		return r;
	}

	template<class... Args>
	Submission submitEncoded(Args&&... args) noexcept
	{
		auto r = client_.submitEncoded(std::forward<Args>(args)...);
		if (r)
			wake();
		return r;
	}

	bool cancel(RequestHandle handle) noexcept
	{
		const bool r = client_.cancel(handle);
		if (r)
			wake();
		return r;
	}

	void close() noexcept
	{
		client_.close();
		wake();
	}
};
} // namespace telemetry::client
#endif // TELEMETRY_LIB_TELEMETRY_CLIENT_THREADSAFECLIENT_HPP
