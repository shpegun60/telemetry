/*
 * @file Dispatcher.cpp
 * @brief Checked Encoded v1 envelope and Model endpoint dispatch.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#include <integration/encoded/Dispatcher.hpp>
#include <telemetry/encoded/Dispatcher.hpp>

namespace telemetry::integration::encoded {
namespace wire = ::telemetry::protocol::encoded;
namespace ts = ::telemetry;

namespace {
struct Extents {
	bool found = false;
	std::uint32_t request = 0;
	std::uint32_t response = 0;
};

Extents extents(ts::ModelView model, const wire::Header& header) noexcept
{
	switch (header.operation) {
		case wire::Operation::FieldRead:
		case wire::Operation::FieldWrite: {
			const auto id = model.fieldTypeId(header.endpoint);
			const auto* type = id ? model.types.find(*id) : nullptr;
			if (type == nullptr)
				return {};
			return header.operation == wire::Operation::FieldRead
			           ? Extents{true, 0, type->wireBytes}
			           : Extents{true, type->wireBytes, 0};
		}
		case wire::Operation::Command: {
			const auto id = model.commandTypeId(header.endpoint);
			const auto* type = id ? model.types.find(*id) : nullptr;
			return type != nullptr ? Extents{true, type->wireBytes, 0} : Extents{};
		}
		case wire::Operation::Service: {
			const auto ids = model.serviceTypeIds(header.endpoint);
			const auto* request = ids ? model.types.find(ids->requestTypeId) : nullptr;
			const auto* response = ids ? model.types.find(ids->responseTypeId) : nullptr;
			return request != nullptr && response != nullptr
			           ? Extents{true, request->wireBytes, response->wireBytes}
			           : Extents{};
		}
	}
	return {};
}
} // namespace

EncodedDispatchResult dispatchEncoded(ts::ModelView model, ts::Workspace& workspace,
                                      std::uint32_t admittedSession,
                                      std::span<const std::byte> request,
                                      std::span<std::byte> response,
                                      ts::detail::CurrentStructuredAbiTag) noexcept
{
	const auto parsed = wire::parse(request);
	if (!parsed || parsed.header.reply)
		return {EncodedDispatchStatus::Malformed, 0,
		        parsed ? wire::Status::InvalidFlags : parsed.status};
	if (admittedSession == 0 || parsed.header.session != admittedSession)
		return {EncodedDispatchStatus::NotAdmitted};
	if (response.size() < wire::headerBytes)
		return {EncodedDispatchStatus::BufferTooSmall};
	if (ts::buffersOverlap(request, response) || ts::buffersOverlap(request, workspace.storage()) ||
	    ts::buffersOverlap(response, workspace.storage()))
		return {EncodedDispatchStatus::Overlap};

	auto reply = parsed.header;
	reply.reply = true;
	reply.payloadBytes = 0;
	const auto sizes = extents(model, parsed.header);
	if (!sizes.found) {
		reply.dispatch = static_cast<std::uint8_t>(ts::DispatchStatus::NotFound);
	} else if (sizes.request != parsed.header.payloadBytes ||
	           sizes.response != parsed.header.expectedResponseBytes) {
		reply.dispatch = static_cast<std::uint8_t>(ts::DispatchStatus::InvalidPayload);
	} else if (response.size() - wire::headerBytes < sizes.response) {
		reply.dispatch = static_cast<std::uint8_t>(ts::DispatchStatus::BufferTooSmall);
	} else {
		const auto output = response.subspan(wire::headerBytes, sizes.response);
		const auto result =
		    ts::dispatchEncoded(model, static_cast<ts::Operation>(parsed.header.operation),
		                        parsed.header.endpoint, parsed.payload, output, workspace);
		reply.dispatch = static_cast<std::uint8_t>(result.dispatch);
		reply.endpointStatus = result.endpointStatus;
		reply.payloadBytes = result.written;
	}
	if (reply.dispatch != 0) {
		reply.endpointStatus = 0;
		reply.payloadBytes = 0;
	}
	// A corrupt endpoint status/count must not become a successful wire result.
	if (wire::validate(reply) != wire::Status::Ok) {
		reply.dispatch = static_cast<std::uint8_t>(ts::DispatchStatus::InternalError);
		reply.endpointStatus = 0;
		reply.payloadBytes = 0;
	}
	(void)wire::writeHeader(response, reply);
	return {EncodedDispatchStatus::Ok, wire::headerBytes + reply.payloadBytes};
}

} // namespace telemetry::integration::encoded
