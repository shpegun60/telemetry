/*
 * @file Exchange.cpp
 * @brief Bounded session routing over existing Field/Command/Service codecs.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#include "Exchange.hpp"
#include "detail/ExchangeWire.hpp"

namespace example::structured_protocol {
namespace {
namespace ts = telemetry::structured;
using D = PacketStatus;

struct Route {
    std::uint32_t requestId;
    telemetry::PackedId endpointId;
    std::uint8_t operation;
};

PacketResult finish(Output response, Route route, D status,
                    std::uint8_t endpoint = 0, std::uint32_t bytes = 0) noexcept
{
    if (status != D::Ok) { endpoint = 0; bytes = 0; }
    detail::magic(response, "TSRP");
    detail::store(response, 4, protocolMajor);
    detail::store(response, 6, protocolMinor);
    detail::store(response, 8, route.requestId);
    detail::store(response, 12, route.endpointId);
    detail::store(response, 16, bytes);
    response[20] = static_cast<std::byte>(route.operation);
    response[21] = static_cast<std::byte>(detail::wireStatus(status));
    response[22] = static_cast<std::byte>(endpoint);
    response[23] = std::byte{0};
    return {status, exchangeHeaderBytes + bytes};
}

bool overlapsScratch(std::uint32_t required, Input request, Output response,
                     const ts::Workspace& workspace) noexcept
{
    // Include envelope bytes, which the payload-only encoded API cannot see.
    // No access to workspace storage when this endpoint is entirely local.
    return required != 0 && (ts::buffersOverlap(request, workspace.storage()) ||
                             ts::buffersOverlap(response, workspace.storage()));
}

PacketResult rejectOverlap(Output response, Route route, const ts::Workspace& workspace) noexcept
{
    // Reporting an overlap must not overwrite a caller's live scratch object.
    // If even the error header aliases scratch, only the local status is safe.
    if (ts::buffersOverlap(response.first(exchangeHeaderBytes), workspace.storage()))
        return {D::InvalidPayload};
    return finish(response, route, D::InvalidPayload);
}
} // namespace

PacketResult Exchange::processImpl(const Binding& peer, Input request, Output response,
    telemetry::structured::Workspace& workspace, detail::CurrentExchangeAbiTag) noexcept
{
    if (response.size() < exchangeHeaderBytes) return {D::BufferTooSmall};
    if (request.size() < exchangeHeaderBytes || !detail::magic(request, "TSRQ"))
        return {D::InvalidRequest}; // No invented request correlation.

    // Capture all routing data before any overlapping response can overwrite it.
    const Route route{detail::load<std::uint32_t>(request, 8),
                      detail::load<std::uint32_t>(request, 12),
                      std::to_integer<std::uint8_t>(request[20])};
    if (detail::load<std::uint16_t>(request, 4) != protocolMajor ||
        detail::load<std::uint16_t>(request, 6) != protocolMinor)
        return finish(response, route, D::UnsupportedVersion);
    if (request[21] != std::byte{0} || request[22] != std::byte{0} || request[23] != std::byte{0} ||
        route.operation < 1 || route.operation > 3)
        return finish(response, route, D::InvalidRequest);
    // Subtract after the minimum-size check: never add an untrusted u32 length.
    if (request.size() - exchangeHeaderBytes != detail::load<std::uint32_t>(request, 16))
        return finish(response, route, D::InvalidPayload);
    if (!peer.ready()) return finish(response, route, D::NotReady);

    const auto payload = request.subspan(exchangeHeaderBytes);
    std::uint8_t endpoint = 0;
    switch (static_cast<Operation>(route.operation)) {
    case Operation::FieldWrite: {
        const auto* entry = peer.model_->fields.find(route.endpointId);
        if (entry == nullptr) return finish(response, route, D::NotFound);
        if (entry->write == nullptr) return finish(response, route, D::Ok, 2); // ReadOnly, before decoding.
        if (payload.size() != entry->wireBytes) return finish(response, route, D::InvalidPayload);
        if (overlapsScratch(entry->scratchBytes, request, response.first(exchangeHeaderBytes), workspace))
            return rejectOverlap(response, route, workspace);
        const auto result = entry->writeEncoded(payload, workspace);
        auto status = detail::packetStatus(result.dispatch);
        if (status == D::Ok && !detail::wireStatus(result.endpointStatus, endpoint)) status = D::InternalError;
        return finish(response, route, status, endpoint);
    }
    case Operation::Command: {
        const auto* entry = peer.model_->commands.find(route.endpointId);
        if (entry == nullptr) return finish(response, route, D::NotFound);
        if (payload.size() != entry->requestWireBytes) return finish(response, route, D::InvalidPayload);
        if (overlapsScratch(entry->scratchBytes, request, response.first(exchangeHeaderBytes), workspace))
            return rejectOverlap(response, route, workspace);
        const auto result = entry->executeEncoded(payload, workspace);
        auto status = detail::packetStatus(result.dispatch);
        if (status == D::Ok && !detail::wireStatus(result.endpointStatus, endpoint)) status = D::InternalError;
        return finish(response, route, status, endpoint);
    }
    case Operation::Service: {
        const auto* entry = peer.model_->services.find(route.endpointId);
        if (entry == nullptr) return finish(response, route, D::NotFound);
        if (payload.size() != entry->requestWireBytes) return finish(response, route, D::InvalidPayload);
        if (entry->responseWireBytes > response.size() - exchangeHeaderBytes ||
            entry->responseWireBytes > UINT32_MAX - exchangeHeaderBytes)
            return finish(response, route, D::BufferTooSmall);
        const auto usedOutput = response.first(exchangeHeaderBytes + entry->responseWireBytes);
        if (overlapsScratch(entry->scratchBytes, request, usedOutput, workspace))
            return rejectOverlap(response, route, workspace);
        const auto result = entry->callEncoded(payload, usedOutput.subspan(exchangeHeaderBytes), workspace);
        auto status = detail::packetStatus(result.dispatch);
        if (status == D::Ok && (!detail::wireStatus(result.endpointStatus, endpoint) ||
            result.written != (result.endpointStatus == ts::ServiceStatus::Ok ? entry->responseWireBytes : 0)))
            status = D::InternalError;
        return finish(response, route, status, endpoint, result.written);
    }
    }
    return finish(response, route, D::InternalError);
}

} // namespace example::structured_protocol
