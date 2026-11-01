/*
 * @file Prototype.hpp
 * @brief Experimental bounded ReadMany frames and shared host-only read execution.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#ifndef TELEMETRY_EXAMPLES_READ_MANY_MAPPING_PROTOTYPE_HPP
#define TELEMETRY_EXAMPLES_READ_MANY_MAPPING_PROTOTYPE_HPP
#pragma once

#include <telemetry/model/Model.hpp>
#include <telemetry/runtime/DynamicValueView.hpp>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

// Experiment only: not a production operation, endpoint, transport or client.
// The owner serializes calls and keeps Model/RuntimeSchema backing storage alive.
// Writable frames and Batch storage are disjoint from Model/schema backing
// storage, native endpoint owners and control objects. Only explicitly checked
// byte-input aliases are diagnosed; arbitrary owner overlap is outside this API.
// Frames deliberately have a private magic and version zero. Existing TEQ1/RPC
// parsers do not accept them; a future production mapping needs separate approval.
namespace read_many_mapping {
using Input = std::span<const std::byte>;
using Output = std::span<std::byte>;
inline constexpr std::size_t maxItems = 32, maxValueBytes = 8, headerBytes = 32;
inline constexpr std::size_t capabilityBytes = 24;
inline constexpr std::uint16_t maxRequestPayload = 130, maxReplyPayload = 514;
inline constexpr std::uint32_t frameMagic = 0x30584d52u;      // RMX0.
inline constexpr std::uint32_t capabilityMagic = 0x30504352u; // RCP0.

enum class GlobalStatus : std::uint8_t {
	Ok,
	Malformed,
	NotAdmitted,
	Duplicate,
	BufferTooSmall,
	Overlap,
	ScratchTooSmall,
	UnsupportedProfile,
	Stale,
	UnknownId,
	UnsupportedType
};
enum class ItemStatus : std::uint8_t {
	Ok,
	NotFound,
	UnsupportedType,
	Unavailable,
	ReadError
};

template<class T>
struct Result {
	GlobalStatus status = GlobalStatus::Malformed;
	T value{};

	[[nodiscard]] explicit operator bool() const noexcept
	{
		return status == GlobalStatus::Ok;
	}
};

struct FrameResult {
	GlobalStatus status = GlobalStatus::Malformed;
	std::size_t written = 0;

	[[nodiscard]] explicit operator bool() const noexcept
	{
		return status == GlobalStatus::Ok;
	}
};

struct Capability {
	std::uint32_t session = 1;
	std::uint64_t fingerprint = 0;
	std::uint8_t itemLimit = 32;
	std::uint16_t flags = 3; // This experiment supports scalars and open enums, read-only.
	std::uint16_t requestLimit = maxRequestPayload, replyLimit = maxReplyPayload;
	friend bool operator==(const Capability&, const Capability&) = default;
};

struct Item {
	std::uint32_t id = 0;
	ItemStatus status = ItemStatus::Ok;
	std::uint8_t length = 0;
	std::array<std::byte, maxValueBytes> value{};
};

struct Batch {
	std::uint16_t count = 0;
	std::array<Item, maxItems> items{};
};

struct FrameHeader {
	bool reply = false;
	GlobalStatus status = GlobalStatus::Ok;
	std::uint32_t session = 0, requestId = 0;
	std::uint64_t fingerprint = 0;
	std::uint16_t payloadBytes = 0, responseBudget = 0;
};

struct Frame {
	FrameHeader header{};
	Input payload{};
};

namespace detail {
template<class T>
[[nodiscard]] constexpr T load(Input bytes, std::size_t offset) noexcept
{
	std::uint64_t value = 0;
	for (std::size_t i = 0; i < sizeof(T); ++i)
		value |= std::uint64_t{std::to_integer<std::uint8_t>(bytes[offset + i])} << (8 * i);
	return static_cast<T>(value);
}

template<class T>
constexpr void store(Output bytes, std::size_t offset, T value) noexcept
{
	for (std::size_t i = 0; i < sizeof(T); ++i)
		bytes[offset + i] = std::byte((static_cast<std::uint64_t>(value) >> (8 * i)) & 0xffu);
}

[[nodiscard]] inline bool overlap(Input first, Input second) noexcept
{
	if (first.empty() || second.empty())
		return false;
	const auto a = reinterpret_cast<std::uintptr_t>(first.data());
	const auto b = reinterpret_cast<std::uintptr_t>(second.data());
	return a <= b ? b - a < first.size() : a - b < second.size();
}

[[nodiscard]] constexpr bool valid(const Capability& capability) noexcept
{
	return capability.session != 0 && capability.itemLimit != 0 &&
	       capability.itemLimit <= maxItems && capability.flags == 3 &&
	       capability.requestLimit >= 6 && capability.requestLimit <= maxRequestPayload &&
	       capability.replyLimit >= 10 && capability.replyLimit <= maxReplyPayload;
}

[[nodiscard]] inline GlobalStatus idsStatus(std::span<const std::uint32_t> ids,
                                            std::size_t itemLimit = maxItems) noexcept
{
	if (ids.empty() || ids.size() > maxItems)
		return GlobalStatus::Malformed;
	if (ids.size() > itemLimit)
		return GlobalStatus::UnsupportedProfile;
	for (std::size_t i = 0; i < ids.size(); ++i)
		for (std::size_t j = 0; j < i; ++j)
			if (ids[i] == ids[j])
				return GlobalStatus::Duplicate;
	return GlobalStatus::Ok;
}

template<class Type>
[[nodiscard]] constexpr bool eligible(const Type* type) noexcept
{
	return type &&
	       (type->kind == telemetry::TypeKind::Scalar || type->kind == telemetry::TypeKind::Enum) &&
	       (type->wireBytes == 1 || type->wireBytes == 2 || type->wireBytes == 4 ||
	        type->wireBytes == 8);
}

inline void writeHeader(Output output, const FrameHeader& header) noexcept
{
	store(output, 0, frameMagic);
	output[4] = std::byte{0};
	output[5] = std::byte(header.reply ? 1 : 0);
	output[6] = std::byte(static_cast<std::uint8_t>(header.status));
	output[7] = std::byte{0};
	store(output, 8, header.session);
	store(output, 12, header.requestId);
	store(output, 16, header.fingerprint);
	store(output, 24, header.payloadBytes);
	store(output, 26, header.responseBudget);
	store(output, 28, std::uint32_t{0});
}
} // namespace detail

[[nodiscard]] inline FrameResult writeCapability(Output output,
                                                 const Capability& capability) noexcept
{
	if (!detail::valid(capability))
		return {GlobalStatus::UnsupportedProfile};
	if (output.size() < capabilityBytes)
		return {GlobalStatus::BufferTooSmall};
	if (detail::overlap(output.first(capabilityBytes),
	                    std::as_bytes(std::span{&capability, std::size_t{1}})))
		return {GlobalStatus::Overlap};
	detail::store(output, 0, capabilityMagic);
	output[4] = std::byte{0};
	output[5] = std::byte{capability.itemLimit};
	detail::store(output, 6, capability.flags);
	detail::store(output, 8, capability.requestLimit);
	detail::store(output, 10, capability.replyLimit);
	detail::store(output, 12, capability.session);
	detail::store(output, 16, capability.fingerprint);
	return {GlobalStatus::Ok, capabilityBytes};
}

[[nodiscard]] inline Result<Capability> parseCapability(Input input) noexcept
{
	if (input.size() != capabilityBytes ||
	    detail::load<std::uint32_t>(input, 0) != capabilityMagic || input[4] != std::byte{0})
		return {};
	Capability capability{
	    detail::load<std::uint32_t>(input, 12),  detail::load<std::uint64_t>(input, 16),
	    std::to_integer<std::uint8_t>(input[5]), detail::load<std::uint16_t>(input, 6),
	    detail::load<std::uint16_t>(input, 8),   detail::load<std::uint16_t>(input, 10)};
	return detail::valid(capability) ? Result<Capability>{GlobalStatus::Ok, capability}
	                                 : Result<Capability>{GlobalStatus::UnsupportedProfile, {}};
}

[[nodiscard]] inline Result<Frame> parseFrame(Input input) noexcept
{
	if (input.size() < headerBytes || detail::load<std::uint32_t>(input, 0) != frameMagic ||
	    input[4] != std::byte{0} || std::to_integer<unsigned>(input[5]) > 1 ||
	    std::to_integer<unsigned>(input[6]) >
	        static_cast<unsigned>(GlobalStatus::UnsupportedType) ||
	    input[7] != std::byte{0} || detail::load<std::uint32_t>(input, 28) != 0)
		return {};
	FrameHeader header{input[5] == std::byte{1},
	                   static_cast<GlobalStatus>(std::to_integer<std::uint8_t>(input[6])),
	                   detail::load<std::uint32_t>(input, 8),
	                   detail::load<std::uint32_t>(input, 12),
	                   detail::load<std::uint64_t>(input, 16),
	                   detail::load<std::uint16_t>(input, 24),
	                   detail::load<std::uint16_t>(input, 26)};
	if (header.session == 0 || header.requestId == 0 ||
	    input.size() - headerBytes != header.payloadBytes ||
	    (!header.reply && header.status != GlobalStatus::Ok) ||
	    (header.reply && header.status != GlobalStatus::Ok && header.payloadBytes != 0))
		return {};
	return {GlobalStatus::Ok, {header, input.subspan(headerBytes)}};
}

// Pure metadata preflight precedes every getter. A global failure leaves output
// unchanged; a failed item publishes no payload, including after a partial thunk
// write. Scratch preflight uses each row's conservative alignment-inclusive bound
// against the remaining Workspace, including any already-live outer lease.
[[nodiscard]] inline GlobalStatus execute(telemetry::ModelView model,
                                          std::span<const std::uint32_t> ids,
                                          telemetry::Workspace& workspace, Batch& output,
                                          std::size_t responseBudget = maxReplyPayload) noexcept
{
	const auto validation = detail::idsStatus(ids);
	if (validation != GlobalStatus::Ok)
		return validation;
	const auto outputStorage = std::as_bytes(std::span{&output, std::size_t{1}});
	if (detail::overlap(std::as_bytes(ids), outputStorage) ||
	    detail::overlap(std::as_bytes(ids), workspace.storage()) ||
	    detail::overlap(outputStorage, workspace.storage()))
		return GlobalStatus::Overlap;
	std::array<const telemetry::FieldEntry*, maxItems> entries{};
	Batch next{};
	next.count = static_cast<std::uint16_t>(ids.size());
	std::size_t needed = 2 + 8 * ids.size(), scratch = 0;
	for (std::size_t i = 0; i < ids.size(); ++i) {
		auto& item = next.items[i];
		item.id = ids[i];
		const auto* entry = model.fields.find(ids[i]);
		if (!entry) {
			item.status = ItemStatus::NotFound;
			continue;
		}
		const auto typeId = model.fieldTypeId(ids[i]);
		const auto* type = typeId ? model.types.find(*typeId) : nullptr;
		if (!detail::eligible(type) || entry->wireBytes != type->wireBytes) {
			item.status = ItemStatus::UnsupportedType;
			continue;
		}
		entries[i] = entry;
		item.length = static_cast<std::uint8_t>(entry->wireBytes);
		needed += item.length; // At most 32 * (8 metadata + 8 scalar) + 2.
		scratch = std::max(scratch, static_cast<std::size_t>(entry->readScratchBytes));
	}
	if (needed > responseBudget || needed > maxReplyPayload)
		return GlobalStatus::BufferTooSmall;
	if (workspace.used() > workspace.storage().size() ||
	    scratch > workspace.storage().size() - workspace.used())
		return GlobalStatus::ScratchTooSmall;
	for (std::size_t i = 0; i < ids.size(); ++i) {
		if (!entries[i])
			continue;
		auto& item = next.items[i];
		const auto result =
		    entries[i]->readEncoded(Output{item.value}.first(item.length), workspace);
		if (result.dispatch != telemetry::DispatchStatus::Ok || result.written != item.length) {
			item.status = result.dispatch == telemetry::DispatchStatus::Unavailable
			                  ? ItemStatus::Unavailable
			                  : ItemStatus::ReadError;
			item.length = 0;
			item.value = {};
		}
	}
	output = next;
	return GlobalStatus::Ok;
}

[[nodiscard]] inline FrameResult
writeRequest(Output output, const Capability& capability, std::uint32_t requestId,
             std::span<const std::uint32_t> ids,
             std::uint16_t responseBudget = maxReplyPayload) noexcept
{
	if (!detail::valid(capability))
		return {GlobalStatus::UnsupportedProfile};
	const auto validation = detail::idsStatus(ids, capability.itemLimit);
	if (validation != GlobalStatus::Ok)
		return {validation};
	if (requestId == 0)
		return {GlobalStatus::Malformed};
	const auto payloadBytes = 2 + 4 * ids.size();
	if (payloadBytes > capability.requestLimit || responseBudget > capability.replyLimit ||
	    responseBudget < 2 + 8 * ids.size() || output.size() < headerBytes + payloadBytes)
		return {GlobalStatus::BufferTooSmall};
	if (detail::overlap(output.first(headerBytes + payloadBytes), std::as_bytes(ids)) ||
	    detail::overlap(output.first(headerBytes + payloadBytes),
	                    std::as_bytes(std::span{&capability, std::size_t{1}})))
		return {GlobalStatus::Overlap};
	detail::store(output, headerBytes, static_cast<std::uint16_t>(ids.size()));
	for (std::size_t i = 0; i < ids.size(); ++i)
		detail::store(output, headerBytes + 2 + 4 * i, ids[i]);
	detail::writeHeader(output, {false, GlobalStatus::Ok, capability.session, requestId,
	                             capability.fingerprint, static_cast<std::uint16_t>(payloadBytes),
	                             responseBudget});
	return {GlobalStatus::Ok, headerBytes + payloadBytes};
}

// Outer failure emits nothing. A parsed/admitted request with a batch preflight
// failure emits a correlated header-only error and returns a successful framing
// result. responseBudget is an experimental ceiling, not legacy exact DTO size.
[[nodiscard]] inline FrameResult dispatch(telemetry::ModelView model,
                                          telemetry::Workspace& workspace,
                                          const Capability& admitted, Input request,
                                          Output response, Batch& results) noexcept
{
	const auto parsed = parseFrame(request);
	if (!parsed || parsed.value.header.reply)
		return {GlobalStatus::Malformed};
	const auto& header = parsed.value.header;
	if (!detail::valid(admitted) || header.session != admitted.session ||
	    header.fingerprint != admitted.fingerprint)
		return {GlobalStatus::NotAdmitted};
	if (response.size() < headerBytes)
		return {GlobalStatus::BufferTooSmall};
	const auto resultStorage = std::as_bytes(std::span{&results, std::size_t{1}});
	if (detail::overlap(request, response) || detail::overlap(request, workspace.storage()) ||
	    detail::overlap(response, workspace.storage()) || detail::overlap(request, resultStorage) ||
	    detail::overlap(response, resultStorage) ||
	    detail::overlap(resultStorage, workspace.storage()))
		return {GlobalStatus::Overlap};
	GlobalStatus status = GlobalStatus::Ok;
	std::array<std::uint32_t, maxItems> ids{};
	std::size_t count = 0;
	if (header.payloadBytes > admitted.requestLimit ||
	    header.responseBudget > admitted.replyLimit) {
		status = GlobalStatus::UnsupportedProfile;
	} else if (parsed.value.payload.size() < 2) {
		status = GlobalStatus::Malformed;
	} else {
		count = detail::load<std::uint16_t>(parsed.value.payload, 0);
		if (count == 0 || count > maxItems || parsed.value.payload.size() != 2 + 4 * count) {
			status = GlobalStatus::Malformed;
		} else if (count > admitted.itemLimit) {
			status = GlobalStatus::UnsupportedProfile;
		} else {
			for (std::size_t i = 0; i < count; ++i)
				ids[i] = detail::load<std::uint32_t>(parsed.value.payload, 2 + 4 * i);
			status = execute(
			    model, std::span<const std::uint32_t>{ids}.first(count), workspace, results,
			    std::min<std::size_t>(header.responseBudget, response.size() - headerBytes));
		}
	}
	auto reply = header;
	reply.reply = true;
	reply.status = status;
	reply.payloadBytes = 0;
	std::size_t cursor = headerBytes;
	if (status == GlobalStatus::Ok) {
		detail::store(response, cursor, results.count);
		cursor += 2;
		for (std::size_t i = 0; i < results.count; ++i) {
			const auto& item = results.items[i];
			detail::store(response, cursor, item.id);
			response[cursor + 4] = std::byte(static_cast<std::uint8_t>(item.status));
			response[cursor + 5] = std::byte{0};
			detail::store(response, cursor + 6, static_cast<std::uint16_t>(item.length));
			if (item.length != 0)
				std::memcpy(response.data() + cursor + 8, item.value.data(), item.length);
			cursor += 8 + item.length;
		}
		reply.payloadBytes = static_cast<std::uint16_t>(cursor - headerBytes);
	}
	detail::writeHeader(response, reply);
	return {GlobalStatus::Ok, cursor};
}

struct CapturedItem {
	std::uint32_t id = 0;
	telemetry::TypeId typeId = 0;
	std::uint8_t wireBytes = 0;
};

class CapturedRequest {
public:
	[[nodiscard]] std::uint16_t count() const noexcept
	{
		return count_;
	}

	[[nodiscard]] std::uint32_t requestId() const noexcept
	{
		return requestId_;
	}

	[[nodiscard]] const Capability& capability() const noexcept
	{
		return capability_;
	}

	[[nodiscard]] std::span<const CapturedItem> entries() const noexcept
	{
		return {entries_.data(), count_};
	}

	[[nodiscard]] const telemetry::runtime::RuntimeSchema& schema() const noexcept
	{
		return schema_;
	}

	[[nodiscard]] bool current(const Capability& currentCapability) const noexcept
	{
		return schema_.valid() && capability_ == currentCapability &&
		       schema_.fingerprint() == capability_.fingerprint && requestId_ != 0;
	}

private:
	telemetry::runtime::RuntimeSchema schema_{};
	Capability capability_{};
	std::uint32_t requestId_ = 0;
	std::uint16_t count_ = 0;
	std::array<CapturedItem, maxItems> entries_{};
	friend Result<CapturedRequest> capture(const telemetry::runtime::RuntimeSchema&,
	                                       const Capability&, std::uint32_t,
	                                       std::span<const std::uint32_t>) noexcept;
};

[[nodiscard]] inline Result<CapturedRequest>
capture(const telemetry::runtime::RuntimeSchema& schema, const Capability& capability,
        std::uint32_t requestId, std::span<const std::uint32_t> ids) noexcept
{
	if (!schema.valid())
		return {GlobalStatus::Stale, {}};
	if (!detail::valid(capability))
		return {GlobalStatus::UnsupportedProfile, {}};
	if (schema.fingerprint() != capability.fingerprint)
		return {GlobalStatus::NotAdmitted, {}};
	const auto validation = detail::idsStatus(ids, capability.itemLimit);
	if (validation != GlobalStatus::Ok)
		return {validation, {}};
	if (requestId == 0)
		return {};
	if (2 + 4 * ids.size() > capability.requestLimit)
		return {GlobalStatus::BufferTooSmall, {}};
	CapturedRequest captured;
	captured.schema_ = schema;
	captured.capability_ = capability;
	captured.requestId_ = requestId;
	captured.count_ = static_cast<std::uint16_t>(ids.size());
	std::size_t needed = 2 + 8 * ids.size();
	const auto endpoints = schema.endpoints(telemetry::descriptor::Category::Field);
	for (std::size_t i = 0; i < ids.size(); ++i) {
		const telemetry::runtime::RuntimeEndpoint* endpoint = nullptr;
		for (const auto& candidate : endpoints)
			if (candidate.id == ids[i]) {
				endpoint = &candidate;
				break;
			}
		if (!endpoint)
			return {GlobalStatus::UnknownId, {}};
		const auto* type = schema.type(endpoint->typeId);
		if (!detail::eligible(type))
			return {GlobalStatus::UnsupportedType, {}};
		captured.entries_[i] = {ids[i], type->id, static_cast<std::uint8_t>(type->wireBytes)};
		needed += type->wireBytes;
	}
	if (needed > capability.replyLimit)
		return {GlobalStatus::BufferTooSmall, {}};
	return {GlobalStatus::Ok, captured};
}

[[nodiscard]] inline FrameResult writeRequest(Output output, const CapturedRequest& request,
                                              const Capability& currentCapability) noexcept
{
	if (!request.current(currentCapability))
		return {GlobalStatus::Stale};
	if (detail::overlap(output, std::as_bytes(std::span{&request, std::size_t{1}})))
		return {GlobalStatus::Overlap};
	std::array<std::uint32_t, maxItems> ids{};
	for (std::size_t i = 0; i < request.count(); ++i)
		ids[i] = request.entries()[i].id;
	return writeRequest(output, request.capability(), request.requestId(),
	                    std::span<const std::uint32_t>{ids}.first(request.count()),
	                    request.capability().replyLimit);
}

struct ReplyItem {
	std::uint32_t id = 0;
	ItemStatus status = ItemStatus::Ok;
	telemetry::TypeId typeId = 0;
	Input payload{};
};

struct ReplyView {
	GlobalStatus operation = GlobalStatus::Ok;
	std::uint16_t count = 0;
	std::array<ReplyItem, maxItems> items{};
};

// All spans borrow the unchanged input frame. No view is returned before every
// item passes structure, order, type and canonical-byte checks. The schema and
// its workspace must obey the existing RuntimeSchema lifetime contract.
[[nodiscard]] inline Result<ReplyView> validateReply(Input input, const CapturedRequest& request,
                                                     const Capability& currentCapability) noexcept
{
	if (!request.current(currentCapability))
		return {GlobalStatus::Stale, {}};
	const auto parsed = parseFrame(input);
	if (!parsed || !parsed.value.header.reply)
		return {};
	const auto& header = parsed.value.header;
	if (header.session != request.capability().session ||
	    header.fingerprint != request.capability().fingerprint ||
	    header.requestId != request.requestId() ||
	    header.responseBudget != request.capability().replyLimit ||
	    header.payloadBytes > header.responseBudget)
		return {};
	ReplyView reply;
	reply.operation = header.status;
	if (header.status != GlobalStatus::Ok)
		return {GlobalStatus::Ok, reply};
	const auto payload = parsed.value.payload;
	if (payload.size() < 2 || detail::load<std::uint16_t>(payload, 0) != request.count())
		return {};
	reply.count = request.count();
	std::size_t cursor = 2;
	for (std::size_t i = 0; i < reply.count; ++i) {
		if (payload.size() - cursor < 8)
			return {};
		const auto id = detail::load<std::uint32_t>(payload, cursor);
		const auto code = std::to_integer<unsigned>(payload[cursor + 4]);
		const auto bytes = detail::load<std::uint16_t>(payload, cursor + 6);
		if (id != request.entries()[i].id || code > static_cast<unsigned>(ItemStatus::ReadError) ||
		    payload[cursor + 5] != std::byte{0})
			return {};
		cursor += 8;
		if (bytes > payload.size() - cursor)
			return {};
		auto& item = reply.items[i];
		item.id = id;
		item.status = static_cast<ItemStatus>(code);
		if (item.status == ItemStatus::Ok) {
			if (bytes != request.entries()[i].wireBytes ||
			    !request.schema().view(request.entries()[i].typeId, payload.subspan(cursor, bytes)))
				return {};
			item.typeId = request.entries()[i].typeId;
			item.payload = payload.subspan(cursor, bytes);
		} else if (bytes != 0) {
			return {};
		}
		cursor += bytes;
	}
	return cursor == payload.size() ? Result<ReplyView>{GlobalStatus::Ok, reply}
	                                : Result<ReplyView>{};
}
} // namespace read_many_mapping
#endif // TELEMETRY_EXAMPLES_READ_MANY_MAPPING_PROTOTYPE_HPP
