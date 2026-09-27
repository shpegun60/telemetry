/*
 * @file ModelEdges.cpp
 * @brief Status origin, void shapes, catalog positions and buffer boundaries.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */

#include <telemetry_structured/model/Model.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace ts = telemetry::structured;

namespace edge {

struct Request { std::uint8_t value; };
struct Response { std::uint32_t value; };

inline int calls = 0;
inline int snapshots = 0;

ts::ServiceResult<Response> answer(const Request& request) noexcept
{
    ++calls;
    if (request.value == 0)
        return ts::ServiceResult<Response>::failure(ts::ServiceStatus::Unavailable);
    return ts::ServiceResult<Response>::success({request.value + 100u});
}

void reset() noexcept { ++calls; }

Response give() noexcept
{
    ++calls;
    return {42};
}

ts::ServiceResult<void> acknowledge(const Request& request) noexcept
{
    ++calls;
    return request.value == 0
        ? ts::ServiceResult<void>::failure(ts::ServiceStatus::Busy)
        : ts::ServiceResult<void>::success();
}

struct SnapshotBinding {
    using Signature = Response(const Request&) noexcept;

    int snapshot() const noexcept { return ++snapshots; }
    static bool available(int) noexcept { return true; }
    static Response invoke(int selected, const Request&) noexcept
    {
        ++calls;
        return {static_cast<std::uint32_t>(selected)};
    }
};

enum class Local : std::uint8_t { Answer, Reset, Acknowledge, Snapshot, Give };

inline constexpr ts::ServiceTable first{
    ts::service<&answer>("Answer"),
    ts::service<&reset>("Reset"),
    ts::service<&acknowledge>("Acknowledge"),
    ts::ServiceDefinition{"Snapshot", SnapshotBinding{}},
    ts::service<&give>("Give")
};

inline constexpr ts::ServiceTable second{ts::service<&answer>("OtherAnswer")};
inline constexpr ts::ServiceCatalogTable catalogs{
    ts::group("first", first), ts::group("second", second)};
inline constexpr ts::Model model{ts::emptyFields, ts::emptyCommands, catalogs};

static_assert(model.typeId<void>() == 0);
static_assert(model.types().count == 14);
static_assert(model.maxServiceResponseWireSize() == 4);
static_assert(model.maxServiceScratch() <= ts::scratchBytes<Request> +
              ts::scratchBytes<ts::ServiceResult<Response>>);
static_assert(ts::maxLocalObjectBytes < sizeof(Request) + sizeof(ts::ServiceResult<Response>) ||
              model.maxServiceScratch() == 0);

} // namespace edge

int main()
{
    constexpr auto answerId = telemetry::makeId<0, 0>();
    constexpr auto resetId = telemetry::makeId<0, 1>();
    constexpr auto acknowledgeId = telemetry::makeId<0, 2>();
    constexpr auto snapshotId = telemetry::makeId<0, 3>();
    constexpr auto giveId = telemetry::makeId<0, 4>();
    constexpr auto otherId = telemetry::makeId<1, 0>();

    if (edge::first.call<edge::Local::Answer>(edge::Request{2}).value().value != 102)
        return 1;
    if (edge::catalogs.call<otherId>(edge::Request{2}).value().value != 102)
        return 2;

    std::array<std::byte, 1> request{std::byte{0}};
    std::array<std::byte, 4> output{};
    std::array<std::byte, 64> scratch{};
    ts::Workspace workspace{scratch};
    const auto index = edge::model.serviceIndex();

    const auto applicationUnavailable = index.callEncoded(answerId, request, output, workspace);
    if (applicationUnavailable.dispatch != ts::DispatchStatus::Ok ||
        applicationUnavailable.endpointStatus != ts::ServiceStatus::Unavailable ||
        applicationUnavailable.written != 0 || workspace.used() != 0 ||
        output != std::array<std::byte, 4>{}) return 3;

    const int beforeBad = edge::calls;
    if (index.callEncoded(answerId, std::span{request}.first(0), output, workspace).dispatch !=
        ts::DispatchStatus::InvalidPayload || edge::calls != beforeBad) return 4;
    if (index.callEncoded(answerId, request, std::span{output}.first(3), workspace).dispatch !=
        ts::DispatchStatus::BufferTooSmall || edge::calls != beforeBad) return 5;

    const auto reset = index.callEncoded(resetId, {}, {}, workspace);
    if (reset.dispatch != ts::DispatchStatus::Ok || reset.written != 0 ||
        edge::calls != beforeBad + 1) return 6;

    const auto busy = index.callEncoded(acknowledgeId, request, {}, workspace);
    if (busy.dispatch != ts::DispatchStatus::Ok ||
        busy.endpointStatus != ts::ServiceStatus::Busy ||
        edge::calls != beforeBad + 2) return 7;

    request[0] = std::byte{4};
    const auto snapshot = index.callEncoded(snapshotId, request, output, workspace);
    if (snapshot.dispatch != ts::DispatchStatus::Ok || edge::snapshots != 1 ||
        snapshot.written != 4 || edge::calls != beforeBad + 3) return 8;

    const auto other = index.callEncoded(otherId, request, output, workspace);
    if (other.dispatch != ts::DispatchStatus::Ok || other.written != 4 ||
        edge::calls != beforeBad + 4) return 9;

    const auto withoutRequest = index.callEncoded(giveId, {}, output, workspace);
    if (withoutRequest.dispatch != ts::DispatchStatus::Ok ||
        withoutRequest.written != 4 || edge::calls != beforeBad + 5 ||
        output[0] != std::byte{42}) return 15;

    if (index.find(telemetry::makeId<2, 0>()) != nullptr ||
        index.find(telemetry::makeId<0, 5>()) != nullptr ||
        index.find(-1) != nullptr ||
        index.find(std::uint64_t{0x100000000ULL}) != nullptr) return 10;
    if (edge::model.view().serviceTypeIds(otherId)->requestTypeId !=
        edge::model.typeId<edge::Request>()) return 11;
    if (edge::model.view().serviceTypeIds(otherId)->responseTypeId !=
        edge::model.typeId<edge::Response>()) return 12;
    if (edge::model.view().serviceTypeIds(std::uint64_t{0x100000000ULL})) return 13;

    std::array<std::byte, 64> shared{};
    ts::Workspace overlappedWorkspace{shared};
    const auto expectedAlias = index.find(answerId)->scratchBytes == 0
        ? ts::DispatchStatus::Ok : ts::DispatchStatus::InvalidPayload;
    if (index.callEncoded(answerId, std::span{request},
                          std::span{shared}.first(4), overlappedWorkspace).dispatch !=
        expectedAlias) return 14;

    std::array<std::byte, 8> aliasedBuffers{};
    aliasedBuffers[0] = std::byte{5};
    // The native Request is independent of the byte buffer before encoding.
    if (index.callEncoded(answerId, std::span{aliasedBuffers}.first(1),
                          std::span{aliasedBuffers}.first(4), workspace).dispatch !=
        ts::DispatchStatus::Ok || aliasedBuffers[0] != std::byte{105}) return 16;

    return 0;
}
