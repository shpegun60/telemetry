/*
 * @file ReadManyDispatcher.cpp
 * @brief Explicit optional Encoded ReadMany mapping, separate from legacy roots.
 * @author Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT
 */
#include <integration/encoded/Dispatcher.hpp>
#include <telemetry/encoded/ReadMany.hpp>
#include <algorithm>

namespace telemetry::integration::encoded {
namespace wire = ::telemetry::protocol::encoded;
namespace ts = ::telemetry;
namespace many = wire::read_many;

namespace {
ts::ReadManyEncodedResult dispatchMany(ts::ModelView model, ts::Workspace& workspace,
                                       const wire::ParseResult& parsed,
                                       std::span<std::byte> response,
                                       const many::Capability& capability) noexcept
{
	if (parsed.payload.size() > capability.requestLimit ||
	    parsed.header.expectedResponseBytes > capability.replyLimit)
		return {};
	const auto request = many::parseRequest(parsed.payload, capability.itemLimit);
	if (!request)
		return {};
	const auto capacity = std::min(response.size() - wire::headerBytes,
	                               std::size_t(parsed.header.expectedResponseBytes));
	return ts::readManyEncoded(model, std::span{request.ids}.first(request.count),
	                           response.subspan(wire::headerBytes, capacity), workspace);
}
} // namespace

static_assert(many::maxItems == ts::readManyMaxItems &&
              many::maxValueBytes == ts::readManyMaxValueBytes &&
              many::maxReplyPayload == ts::readManyMaxResponseBytes);
static_assert(static_cast<unsigned>(many::ItemStatus::Ok) ==
                  static_cast<unsigned>(ts::ReadManyItemStatus::Ok) &&
              static_cast<unsigned>(many::ItemStatus::NotFound) ==
                  static_cast<unsigned>(ts::ReadManyItemStatus::NotFound) &&
              static_cast<unsigned>(many::ItemStatus::UnsupportedType) ==
                  static_cast<unsigned>(ts::ReadManyItemStatus::UnsupportedType) &&
              static_cast<unsigned>(many::ItemStatus::Unavailable) ==
                  static_cast<unsigned>(ts::ReadManyItemStatus::Unavailable) &&
              static_cast<unsigned>(many::ItemStatus::ReadError) ==
                  static_cast<unsigned>(ts::ReadManyItemStatus::ReadError));

EncodedDispatchResult
dispatchEncoded(ts::ModelView model, ts::Workspace& workspace, std::uint32_t admittedSession,
                std::span<const std::byte> request, std::span<std::byte> response,
                const many::Capability& capability, ts::detail::CurrentStructuredAbiTag,
                detail::CurrentReadManyDispatchLayoutTag) noexcept
{
	const auto parsed = wire::parse(request);
	if (!parsed || parsed.header.reply)
		return {EncodedDispatchStatus::Malformed, 0,
		        parsed ? wire::Status::InvalidFlags : parsed.status};
	if (parsed.header.operation != wire::Operation::ReadMany)
		return dispatchEncoded(model, workspace, admittedSession, request, response);
	if (admittedSession == 0 || parsed.header.session != admittedSession ||
	    !many::valid(capability) || capability.session != admittedSession)
		return {EncodedDispatchStatus::NotAdmitted};
	if (response.size() < wire::headerBytes)
		return {EncodedDispatchStatus::BufferTooSmall};
	const auto control = std::as_bytes(std::span{&capability, std::size_t{1}});
	if (ts::buffersOverlap(request, response) || ts::buffersOverlap(request, workspace.storage()) ||
	    ts::buffersOverlap(response, workspace.storage()) ||
	    ts::buffersOverlap(control, response) || ts::buffersOverlap(control, workspace.storage()))
		return {EncodedDispatchStatus::Overlap};

	const auto result = dispatchMany(model, workspace, parsed, response, capability);
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
