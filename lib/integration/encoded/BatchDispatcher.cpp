/*
 * @file BatchDispatcher.cpp
 * @brief Checked complete-frame mapping for explicitly admitted TBM1 batches.
 * @author Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT
 */
#include <integration/encoded/BatchDispatcher.hpp>
#include <telemetry/encoded/Batch.hpp>
#include <algorithm>
#include <array>

namespace telemetry::integration::encoded {
namespace wire = ::telemetry::protocol::encoded;
namespace ts = ::telemetry;
namespace batch = wire::batch;

static_assert(batch::maxItems == ts::batchMaxItems &&
              batch::maxRequestPayload == ts::batchMaxRequestBytes &&
              batch::maxReplyPayload == ts::batchMaxResponseBytes);

EncodedDispatchResult
dispatchEncoded(ts::ModelView model, ts::Workspace& workspace, std::uint32_t admittedSession,
                std::span<const std::byte> request, std::span<std::byte> response,
                const batch::Capability& capability, ts::detail::CurrentStructuredAbiTag,
                detail::CurrentBatchDispatchLayoutTag) noexcept
{
	const auto parsed = wire::parse(request);
	if (!parsed || parsed.header.reply)
		return {EncodedDispatchStatus::Malformed, 0,
		        parsed ? wire::Status::InvalidFlags : parsed.status};
	if (parsed.header.operation != wire::Operation::Batch)
		return dispatchEncoded(model, workspace, admittedSession, request, response);
	if (admittedSession == 0 || parsed.header.session != admittedSession ||
	    !batch::valid(capability) || capability.session != admittedSession)
		return {EncodedDispatchStatus::NotAdmitted};
	if (response.size() < wire::headerBytes)
		return {EncodedDispatchStatus::BufferTooSmall};
	const auto control = std::as_bytes(std::span{&capability, std::size_t{1}});
	if (ts::buffersOverlap(request, response) || ts::buffersOverlap(request, workspace.storage()) ||
	    ts::buffersOverlap(response, workspace.storage()) ||
	    ts::buffersOverlap(control, response) || ts::buffersOverlap(control, workspace.storage()))
		return {EncodedDispatchStatus::Overlap};

	ts::BatchEncodedResult result{};
	if (parsed.payload.size() <= capability.requestLimit &&
	    parsed.header.expectedResponseBytes <= capability.replyLimit) {
		const auto body = batch::parseRequest(parsed.payload, capability.itemLimit);
		if (body) {
			std::array<ts::BatchEncodedItem, batch::maxItems> items{};
			for (std::size_t i = 0; i < body.count; ++i)
				items[i] = {static_cast<ts::Operation>(body.items[i].operation), body.items[i].id,
				            body.items[i].payload};
			const auto capacity = std::min(response.size() - wire::headerBytes,
			                               std::size_t(parsed.header.expectedResponseBytes));
			result = ts::batchEncoded(model, std::span{items}.first(body.count),
			                          response.subspan(wire::headerBytes, capacity), workspace);
		}
	}
	auto reply = parsed.header;
	reply.reply = true;
	reply.dispatch = static_cast<std::uint8_t>(result.dispatch);
	reply.payloadBytes = result.dispatch == ts::DispatchStatus::Ok ? result.written : 0;
	if (wire::validate(reply) != wire::Status::Ok) {
		reply.dispatch = static_cast<std::uint8_t>(ts::DispatchStatus::InternalError);
		reply.payloadBytes = 0;
	}
	(void)wire::writeHeader(response, reply);
	return {EncodedDispatchStatus::Ok, wire::headerBytes + reply.payloadBytes};
}
} // namespace telemetry::integration::encoded
