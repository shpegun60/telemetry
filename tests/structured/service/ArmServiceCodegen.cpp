/*
 * @file ArmServiceCodegen.cpp
 * @brief Real Service bindings returning a 4 KiB aggregate on Cortex-M7.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */

#include <telemetry/service/Service.hpp>

#include <array>
#include <cstddef>
#include <cstdint>

namespace service_arm {

struct Request {
	std::uint8_t channel;
};

struct BigResponse {
	std::array<std::uint8_t, 4096> bytes;
};

struct SmallResponse {
	std::uint32_t value;
};

// Small-response owner exposes a const method for direct and OwnerSlot ARM probes.
// API: read().
struct Device {
	std::uint32_t base;

	SmallResponse read(const Request& request) const noexcept
	{
		return {base + request.channel};
	}
};

Device device{100};

BigResponse raw(const Request& request) noexcept
{
	BigResponse response{};
	response.bytes[0] = request.channel;
	return response;
}

telemetry::ServiceResult<BigResponse> wrapped(const Request& request) noexcept
{
	return telemetry::ServiceResult<BigResponse>::successFrom([&request]() -> BigResponse {
		BigResponse response{};
		response.bytes[0] = request.channel;
		return response;
	});
}

inline constexpr auto rawService = telemetry::service<&raw>("Raw");
inline constexpr auto wrappedService = telemetry::service<&wrapped>("Wrapped");
inline constexpr auto directOwnerService = telemetry::service<&Device::read>("DirectOwner", device);

} // namespace service_arm

static_assert(sizeof(service_arm::BigResponse) == 4096);
static_assert(sizeof(telemetry::ServiceResult<service_arm::BigResponse>) >= 4097);

// The runner reads these six little-endian words from the object section.
// They measure the actual target layout rather than host sizeof values.
extern "C" [[gnu::used, gnu::section(".rodata.service_layout")]]
const std::uint32_t service_layout[] = {
    sizeof(telemetry::ServiceResult<service_arm::BigResponse>),
    alignof(telemetry::ServiceResult<service_arm::BigResponse>),
    sizeof(service_arm::rawService),
    alignof(decltype(service_arm::rawService)),
    sizeof(service_arm::directOwnerService),
    alignof(decltype(service_arm::directOwnerService)),
};

extern "C" __attribute__((noinline)) telemetry::ServiceResult<service_arm::BigResponse>
call_big_raw(const service_arm::Request& request) noexcept
{
	return service_arm::rawService.call(request);
}

extern "C" __attribute__((noinline)) telemetry::ServiceResult<service_arm::BigResponse>
call_big_wrapped(const service_arm::Request& request) noexcept
{
	return service_arm::wrappedService.call(request);
}

extern "C" __attribute__((noinline)) telemetry::ServiceResult<service_arm::SmallResponse>
call_direct_owner(const service_arm::Request& request) noexcept
{
	return service_arm::directOwnerService.call(request);
}
