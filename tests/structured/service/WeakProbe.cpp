/*
 * @file WeakProbe.cpp
 * @brief ELF weak Service target absence, definition and override checks.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */

#include <telemetry/service/Service.hpp>

#include <cstdint>

struct WeakRequest {
	std::uint8_t id;
};

struct WeakResponse {
	std::uint8_t value;
};

// Weak method owner tests missing targets, fallback definitions and strong replacement.
// API: absent(), replaceable() are optional ELF weak methods.
struct WeakDevice {
	WeakResponse absent(const WeakRequest&) const noexcept __attribute__((weak));
	WeakResponse replaceable(const WeakRequest&) const noexcept;
};

__attribute__((weak)) WeakResponse WeakDevice::replaceable(const WeakRequest&) const noexcept
{
	return {33};
}

extern "C" WeakResponse absent_service(const WeakRequest&) noexcept __attribute__((weak));

extern "C" WeakResponse replaceable_service(const WeakRequest&) noexcept __attribute__((weak));

extern "C" __attribute__((weak)) WeakResponse replaceable_service(const WeakRequest&) noexcept
{
	return {11};
}

int main()
{
	auto absent = telemetry::service<&absent_service>("Absent");
	if (absent.call(WeakRequest{1}).status() != telemetry::ServiceStatus::Unavailable)
		return 1;

	auto replaceable = telemetry::service<&replaceable_service>("Replaceable");
	if (replaceable.call(WeakRequest{1}).value().value != EXPECT_OVERRIDE)
		return 2;

	WeakDevice device{};
	auto absentMethod = telemetry::service<&WeakDevice::absent>("AbsentMethod", device);
	if (absentMethod.call(WeakRequest{1}).status() != telemetry::ServiceStatus::Unavailable)
		return 3;

	auto replaceableMethod =
	    telemetry::service<&WeakDevice::replaceable>("ReplaceableMethod", device);
	constexpr auto expectedMethod = EXPECT_OVERRIDE == 11 ? 33 : 44;
	if (replaceableMethod.call(WeakRequest{1}).value().value != expectedMethod)
		return 4;
	return 0;
}
