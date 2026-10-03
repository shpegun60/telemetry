/*
 * @file ServiceNegative.cpp
 * @brief Compile-time Service signature and borrowing rejection cases.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */

#include <telemetry/service/Service.hpp>

#include <cstdint>

struct Request { std::uint16_t id; };
struct Response { std::uint16_t value; };

Response scalarRequest(int) noexcept { return {}; }
int scalarResponse(const Request&) noexcept { return 1; }
Response mutableRequest(Request&) noexcept { return {}; }
Response throwingRequest(const Request&) { return {}; }
Response twoArguments(const Request&, int) noexcept { return {}; }
Response* pointerResponse() noexcept { return nullptr; }
Response good(const Request&) noexcept { return {}; }
const Response constResponse(const Request&) noexcept { return {}; }

struct Device {
    Response read(const Request&) const noexcept { return {}; }
    Response write(const Request&) noexcept { return {}; }
};

struct DeviceProxy {
    operator Device() const noexcept { return {}; }
};

struct DeviceHolder {
    Device device{};
    operator const Device&() const noexcept { return device; }
};

struct StatefulPlus {
    int state;
    using Function = Response (*)(const Request&) noexcept;
    Function operator+() const noexcept { return &good; }
    Response operator()(const Request&) const noexcept { return {}; }
};

constexpr auto nullFunction =
    static_cast<Response (*)(const Request&) noexcept>(nullptr);
constexpr auto nullMethod =
    static_cast<Response (Device::*)(const Request&) const noexcept>(nullptr);

#if CASE == 1
auto invalid = telemetry::service<&scalarRequest>("Invalid");
#elif CASE == 2
auto invalid = telemetry::service<&scalarResponse>("Invalid");
#elif CASE == 3
auto invalid = telemetry::service<&mutableRequest>("Invalid");
#elif CASE == 4
auto invalid = telemetry::service<&throwingRequest>("Invalid");
#elif CASE == 5
auto invalid = telemetry::service<&twoArguments>("Invalid");
#elif CASE == 6
auto invalid = telemetry::service<&pointerResponse>("Invalid");
#elif CASE == 7
auto invalid = telemetry::service<&Device::read>("Invalid", Device{});
#elif CASE == 8
auto invalid = telemetry::service("Invalid", [value = 1](const Request&) noexcept {
    return Response{static_cast<std::uint16_t>(value)};
});
#elif CASE == 9
auto invalid = telemetry::service("Invalid", &good, 42);
#elif CASE == 10
const Device device{};
auto invalid = telemetry::service<&Device::write>("Invalid", device);
#elif CASE == 11
auto invalid = telemetry::service<&Device::read, const Device>("Invalid", {});
#elif CASE == 12
auto invalid = telemetry::service("Invalid",
    telemetry::FunctionSlot<Response(const Request&) noexcept>{});
#elif CASE == 13
auto invalid = telemetry::service<&Device::read, const Device>(
    "Invalid", {DeviceProxy{}});
#elif CASE == 14
auto invalid = telemetry::service<&Device::read, const Device>(
    "Invalid", {Device{}});
#elif CASE == 15
DeviceProxy proxy{};
auto invalid = telemetry::service<&Device::read, const Device>(
    "Invalid", proxy);
#elif CASE == 16
DeviceHolder holder{};
auto invalid = telemetry::service<&Device::read, const Device>(
    "Invalid", holder);
#elif CASE == 17
auto invalid = telemetry::service<&constResponse>("Invalid");
#elif CASE == 18
auto invalid = telemetry::service("Invalid", StatefulPlus{1});
#elif CASE == 19
auto invalid = telemetry::service<nullFunction>("Invalid");
#elif CASE == 20
Device device{};
auto invalid = telemetry::service<nullMethod>("Invalid", device);
#else
#error Select an invalid service case
#endif
