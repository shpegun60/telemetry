/* Optional single-owner FreeRTOS task port. Authors: Ruslan Kovtun (shpegun60), codexAi. MIT. */
#ifndef TELEMETRY_EXAMPLES_ASYNC_CLIENT_FREERTOSPORT_HPP
#define TELEMETRY_EXAMPLES_ASYNC_CLIENT_FREERTOSPORT_HPP
#pragma once
#include <FreeRTOS.h>
#include <semphr.h>
#include <task.h>
#include <device_api/ClientBindings.hpp>
#include <telemetry/client/ThreadSafeClient.hpp>
#include <array>
#include <atomic>

#if configSUPPORT_STATIC_ALLOCATION != 1 || configUSE_MUTEXES != 1
#error "FreeRtosPort requires static allocation and mutex support"
#endif
#if INCLUDE_vTaskDelete != 1
#error "FreeRtosPort requires vTaskDelete for coordinator-side owner join"
#endif

namespace async_example {
namespace cl = ::telemetry::client;

class StaticMutex {
	StaticSemaphore_t storage_{};
	SemaphoreHandle_t handle_;

public:
	StaticMutex() noexcept;
	~StaticMutex();
	StaticMutex(const StaticMutex&) = delete;
	StaticMutex& operator=(const StaticMutex&) = delete;
	void lock() noexcept;
	void unlock() noexcept;
};

// All hooks run on the owner task. RX interrupts only publish transport events.
// send accepts a COMPLETE packet; txAvailable retains the prior port TX copy
// until DMA/transport release. closeAndDrain also excludes late old-session RX.
struct Hooks {
	void (*poll)() noexcept;
	bool (*txAvailable)(std::size_t) noexcept;
	bool (*send)(cl::Input, std::uint32_t, std::uint32_t) noexcept;
	bool (*reply)(cl::Output, std::size_t&, std::uint32_t&, std::uint32_t&) noexcept;
	void (*closeAndDrain)() noexcept;
};

class FreeRtosPort {
public:
	using Client = ::device_api::client::DeviceClient<4, 256, 64, StaticMutex>;
	using Producers = cl::ThreadSafeClient<Client>;

	FreeRtosPort(cl::Backend& backend, cl::Clock clock, Hooks hooks) noexcept;
	~FreeRtosPort();
	FreeRtosPort(const FreeRtosPort&) = delete;
	FreeRtosPort& operator=(const FreeRtosPort&) = delete;

	// Start once, from a coordinator task; storage/backend/hooks outlive the join.
	bool start(cl::Input verifiedHeader, std::uint32_t generation, UBaseType_t priority) noexcept;
	cl::Status admissionStatus() const noexcept;
	Producers& producers() noexcept;
	cl::Submission requestConfig(void (*completed)(
	    const cl::Result<::device_api::device::types::Config>&) noexcept) noexcept;
	// Task context only. FIRST stop/join every producer and return from all its
	// submissions. Never call from the owner or one of its callbacks.
	void stopAndJoin() noexcept;

private:
	cl::Backend& backend_;
	cl::Clock clock_;
	Hooks hooks_;
	Client client_;
	Producers producers_;
	StaticSemaphore_t doneStorage_{};
	SemaphoreHandle_t done_;
	StaticTask_t taskStorage_{};
	std::array<StackType_t, 1024> stack_{};
	std::array<std::byte, 64> header_{};
	std::array<std::byte, 1024> tx_{}, rx_{};
	std::atomic<TaskHandle_t> owner_{nullptr};
	std::atomic<cl::Status> admission_{cl::Status::NotAdmitted};
	std::atomic<bool> stopping_{false};
	std::uint32_t generation_ = 0;
	bool started_ = false;

	static void wake(void* self) noexcept;
	static void entry(void* self) noexcept;
	void run() noexcept;
	void step() noexcept;
};
} // namespace async_example
#endif // TELEMETRY_EXAMPLES_ASYNC_CLIENT_FREERTOSPORT_HPP
