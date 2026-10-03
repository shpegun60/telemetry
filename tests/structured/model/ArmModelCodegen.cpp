/*
 * @file ArmModelCodegen.cpp
 * @brief Compare direct, local and global native Service calls on Cortex-M7.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */

#include <telemetry/model/Model.hpp>

#include <array>
#include <cstddef>
#include <cstdint>

namespace arm_model {

struct Request { std::uint32_t code; };
struct Response { std::uint32_t result; };

struct Device {
    std::uint32_t base;
    Response read(const Request& request) const noexcept
    {
        return {base + request.code};
    }
};

Device device{100};
inline constexpr telemetry::ServiceTable local{
    telemetry::service<&Device::read>("Read", device)};
inline constexpr telemetry::ServiceCatalogTable services{
    telemetry::group("device", local)};
inline constexpr telemetry::Model model{
    telemetry::emptyFields,
    telemetry::emptyCommands, services};

} // namespace arm_model

extern "C" __attribute__((noinline))
telemetry::ServiceResult<arm_model::Response>
model_direct(const arm_model::Request& request) noexcept
{
    return telemetry::ServiceResult<arm_model::Response>::successFrom(
        [&]() -> arm_model::Response { return arm_model::device.read(request); });
}

extern "C" __attribute__((noinline))
telemetry::ServiceResult<arm_model::Response>
model_local(const arm_model::Request& request) noexcept
{
    return arm_model::local.call<0>(request);
}

extern "C" __attribute__((noinline))
telemetry::ServiceResult<arm_model::Response>
model_global(const arm_model::Request& request) noexcept
{
    return arm_model::services.call<telemetry::makeId<0, 0>()>(request);
}

extern "C" __attribute__((noinline))
telemetry::EncodedCallResult
model_encoded(std::span<const std::byte> input, std::span<std::byte> output,
              telemetry::Workspace& workspace) noexcept
{
    return arm_model::model.serviceIndex().callEncoded(
        telemetry::makeId<0, 0>(), input, output, workspace);
}
