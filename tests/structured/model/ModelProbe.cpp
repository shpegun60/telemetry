/*
 * @file ModelProbe.cpp
 * @brief Three Service call paths and encoded preflight checks.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */

#include <telemetry_structured/Structured.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

namespace ts = telemetry::structured;

namespace probe {

struct ReadCalibrationRequest {
    std::uint16_t channel;
    bool adjusted;
};

struct ReadCalibrationResponse {
    std::uint32_t scale;
    bool adjusted;
};

inline int calls = 0;

struct Device {
    std::uint32_t base = 100;

    ReadCalibrationResponse readCalibration(const ReadCalibrationRequest& request) const noexcept
    {
        ++calls;
        return {base + request.channel, request.adjusted};
    }
};

inline Device device{};
inline telemetry::OwnerSlot<Device> ownerSlot;

inline constexpr ts::ServiceTable local{
    ts::service<&Device::readCalibration>("ReadCalibration", device),
    ts::service<&Device::readCalibration>("LateRead", ownerSlot)
};

inline constexpr ts::ServiceCatalogTable services{ts::group("calibration", local)};
inline constexpr ts::Model model{ts::emptyFields, ts::emptyCommands, services};

static_assert(local.size() == 2);
static_assert(services.size() == 1);
static_assert(model.types().count >= 14);
static_assert(model.template typeId<ReadCalibrationRequest>() !=
              model.template typeId<ReadCalibrationResponse>());
static_assert(model.maxServiceResponseWireSize() == 5);
static_assert(!std::is_copy_constructible_v<decltype(local)>);
static_assert(!std::is_move_constructible_v<decltype(services)>);

} // namespace probe

int main()
{
    using Request = probe::ReadCalibrationRequest;
    using Response = probe::ReadCalibrationResponse;
    constexpr auto id = telemetry::makeId<0, 0>();
    constexpr auto slotId = telemetry::makeId<0, 1>();
    const Request request{7, true};

    auto local = probe::local.call<0>(request);
    auto global = probe::services.call<id>(request);
    if (local.value().scale != 107 || global.value().scale != 107 ||
        probe::calls != 2) return 1;

    std::array<std::byte, ts::wireSize<Request>> input{};
    std::array<std::byte, ts::wireSize<Response>> output{};
    std::array<std::byte, 128> scratch{};
    ts::Workspace workspace{scratch};
    if (ts::encode(request, input) != ts::CodecStatus::Ok) return 2;

    const auto index = probe::model.serviceIndex();
    auto encoded = index.callEncoded(id, input, output, workspace);
    if (encoded.dispatch != ts::DispatchStatus::Ok ||
        encoded.endpointStatus != ts::ServiceStatus::Ok ||
        encoded.written != output.size() || probe::calls != 3 ||
        workspace.used() != 0) return 3;
    {
        auto responseLease = workspace.reserve<Response>();
        Response* response = nullptr;
        if (ts::decode(output, responseLease, response) != ts::CodecStatus::Ok ||
            response->scale != 107 || !response->adjusted) return 4;
    }

    const int before = probe::calls;
    const auto priorOutput = output;
    if (index.callEncoded(id, input, std::span{output}.first(4), workspace).dispatch !=
        ts::DispatchStatus::BufferTooSmall || probe::calls != before ||
        output != priorOutput || workspace.used() != 0) return 5;
    std::array<std::byte, 1> tiny{};
    ts::Workspace small{tiny};
    if (index.callEncoded(id, input, output, small).dispatch !=
        ts::DispatchStatus::WorkspaceTooSmall || probe::calls != before ||
        output != priorOutput) return 6;

    input.back() = std::byte{2};
    if (index.callEncoded(id, input, output, workspace).dispatch !=
        ts::DispatchStatus::InvalidPayload || probe::calls != before ||
        output != priorOutput || workspace.used() != 0) return 7;
    if (index.callEncoded(slotId, input, output, workspace).dispatch !=
        ts::DispatchStatus::InvalidPayload || probe::calls != before) return 14;
    input.back() = std::byte{1};

    if (index.callEncoded(slotId, input, output, workspace).dispatch !=
        ts::DispatchStatus::Unavailable || probe::calls != before) return 8;
    probe::ownerSlot.bind(probe::device);
    if (index.callEncoded(slotId, input, output, workspace).dispatch !=
        ts::DispatchStatus::Ok || probe::calls != before + 1) return 9;
    probe::ownerSlot.reset();

    if (index.find(std::uint64_t{0x100000000ULL}) != nullptr ||
        index.callEncoded(std::uint64_t{0x100000000ULL}, input, output, workspace).dispatch !=
            ts::DispatchStatus::NotFound) return 10;
    if (probe::model.view().serviceTypeIds(id)->requestTypeId !=
        probe::model.template typeId<Request>()) return 11;
    if (probe::model.view().serviceTypeIds(id)->responseTypeId !=
        probe::model.template typeId<Response>()) return 12;
    auto viaAdapter = ts::callServiceEncoded(probe::model.view(), id,
                                             input, output, workspace);
    if (viaAdapter.dispatch != ts::DispatchStatus::Ok ||
        viaAdapter.written != output.size() || probe::calls != before + 2) return 13;
    return 0;
}
