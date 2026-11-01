/*
 * @file Batch.cpp
 * @brief Global metadata/payload preflight and sequential mixed encoded execution.
 * @author Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT
 */
#include <telemetry/encoded/Batch.hpp>
#include <algorithm>
#include <array>

namespace telemetry {
namespace {
using Input = std::span<const std::byte>;
using Output = std::span<std::byte>;

template<class T>
void store(Output output, std::size_t offset, T value) noexcept
{
	for (std::size_t i = 0; i < sizeof(T); ++i)
		output[offset + i] = std::byte((static_cast<std::uint64_t>(value) >> (8 * i)) & 0xffu);
}

std::uint32_t scalarBytes(ScalarCode code) noexcept
{
	switch (code) {
		case ScalarCode::Bool:
		case ScalarCode::U8:
		case ScalarCode::S8:
			return 1;
		case ScalarCode::U16:
		case ScalarCode::S16:
			return 2;
		case ScalarCode::U32:
		case ScalarCode::S32:
		case ScalarCode::F32:
			return 4;
		case ScalarCode::U64:
		case ScalarCode::S64:
		case ScalarCode::F64:
			return 8;
	}
	return 0;
}

// Walk trusted native-model metadata without decoding native objects or using
// RuntimeSchema. Only Bool rejects raw bit patterns; enums are open and floats
// preserve all bits. Even forged cycles/expansions terminate at structural limits.
bool walk(TypeRegistryView types, TypeId id, Input value, bool inspect, std::uint32_t depth,
          std::uint32_t& visits) noexcept
{
	if (depth > Limits::maxTypeDepth || visits == 0)
		return false;
	--visits;
	const auto* type = types.find(id);
	if (type == nullptr || type->id != id || type->wireBytes > Limits::maxValueWireBytes ||
	    (inspect && value.size() != type->wireBytes))
		return false;
	switch (type->kind) {
		case TypeKind::Void:
			return type->wireBytes == 0;
		case TypeKind::Scalar:
			return scalarBytes(type->scalarCode) == type->wireBytes && type->wireBytes != 0 &&
			       (!inspect || type->scalarCode != ScalarCode::Bool ||
			        std::to_integer<unsigned>(value[0]) <= 1);
		case TypeKind::Enum: {
			const auto* base = types.find(type->relatedTypeId);
			return base != nullptr && base->kind == TypeKind::Scalar &&
			       base->scalarCode >= ScalarCode::U8 && base->scalarCode <= ScalarCode::S64 &&
			       scalarBytes(base->scalarCode) == base->wireBytes &&
			       base->wireBytes == type->wireBytes;
		}
		case TypeKind::Struct: {
			if (type->memberCount > Limits::maxStructMembers ||
			    (type->memberCount != 0 && type->memberData == nullptr))
				return false;
			std::size_t cursor = 0;
			for (std::size_t i = 0; i < type->memberCount; ++i) {
				const auto* member = types.find(type->memberData[i].typeId);
				if (member == nullptr || member->kind == TypeKind::Void ||
				    cursor > type->wireBytes || member->wireBytes > type->wireBytes - cursor ||
				    !walk(types, member->id,
				          inspect ? value.subspan(cursor, member->wireBytes) : Input{}, inspect,
				          depth + 1, visits))
					return false;
				cursor += member->wireBytes;
			}
			return cursor == type->wireBytes;
		}
		case TypeKind::Array: {
			const auto* element = types.find(type->relatedTypeId);
			if (element == nullptr || element->kind == TypeKind::Void ||
			    type->elementCount > Limits::maxArrayElements ||
			    std::uint64_t{element->wireBytes} * type->elementCount != type->wireBytes)
				return false;
			// Shape validation and zero-wire subtrees need one traversal, not an
			// expansion proportional to an arbitrarily large empty array.
			if (!inspect || element->wireBytes == 0 || type->elementCount == 0)
				return walk(types, element->id, {}, false, depth + 1, visits);
			for (std::size_t i = 0; i < type->elementCount; ++i)
				if (!walk(types, element->id,
				          value.subspan(i * element->wireBytes, element->wireBytes), true,
				          depth + 1, visits))
					return false;
			return true;
		}
	}
	return false;
}

bool validType(TypeRegistryView types, TypeId id, Input value = {}, bool inspect = false) noexcept
{
	auto visits = Limits::maxExpandedValueNodes;
	return walk(types, id, value, inspect, 0, visits);
}

struct Plan {
	TypeId responseType = 0;
	std::uint32_t responseBytes = 0;
};

bool validResult(Operation operation, const EncodedDispatchResult& result,
                 std::size_t responseBytes) noexcept
{
	const auto dispatch = static_cast<unsigned>(result.dispatch);
	if (!(dispatch == 0 || (dispatch >= 4 && dispatch <= 9)))
		return false;
	if (dispatch != 0)
		return result.endpointStatus == 0 && result.written == 0;
	switch (operation) {
		case Operation::FieldWrite:
			return result.endpointStatus <= 5 && result.written == 0;
		case Operation::Command:
			return result.endpointStatus <= 7 && result.written == 0;
		case Operation::Service:
			return result.endpointStatus <= 4 &&
			       result.written == (result.endpointStatus == 0 ? responseBytes : 0);
		case Operation::FieldRead:
			return result.endpointStatus == 0 && result.written == responseBytes;
	}
	return false;
}
} // namespace

BatchEncodedResult batchEncoded(ModelView model, std::span<const BatchEncodedItem> items,
                                Output output, Workspace& workspace,
                                detail::CurrentStructuredAbiTag,
                                detail::CurrentBatchEncodedLayoutTag) noexcept
{
	if (items.empty() || items.size() > batchMaxItems)
		return {};
	if (buffersOverlap(std::as_bytes(items), output) ||
	    buffersOverlap(std::as_bytes(items), workspace.storage()) ||
	    buffersOverlap(output, workspace.storage()))
		return {};
	for (const auto& item : items)
		if (buffersOverlap(item.payload, output) ||
		    buffersOverlap(item.payload, workspace.storage()))
			return {};
	std::array<Plan, batchMaxItems> plans{};
	std::size_t responseNeeded = 2 + 12 * items.size();
	std::size_t requestNeeded = 2 + 8 * items.size();
	std::size_t scratch = 0;
	for (std::size_t i = 0; i < items.size(); ++i) {
		const auto& item = items[i];
		if (item.payload.size() > batchMaxRequestBytes - requestNeeded)
			return {};
		requestNeeded += item.payload.size();
		const TypeDescriptor* requestType = nullptr;
		const TypeDescriptor* responseType = nullptr;
		switch (item.operation) {
			case Operation::FieldRead:
			case Operation::FieldWrite: {
				const auto* entry = model.fields.find(item.id);
				const auto id = model.fieldTypeId(item.id);
				const auto* type = id ? model.types.find(*id) : nullptr;
				if (entry == nullptr || type == nullptr)
					return {DispatchStatus::NotFound};
				if (entry->wireBytes != type->wireBytes || type->kind == TypeKind::Void)
					return {};
				if (item.operation == Operation::FieldWrite) {
					if (entry->write == nullptr)
						return {};
					requestType = type;
					scratch = std::max(scratch, std::size_t(entry->writeScratchBytes));
				} else {
					responseType = type;
					scratch = std::max(scratch, std::size_t(entry->readScratchBytes));
				}
				break;
			}
			case Operation::Command: {
				const auto* entry = model.commands.find(item.id);
				const auto id = model.commandTypeId(item.id);
				requestType = id ? model.types.find(*id) : nullptr;
				if (entry == nullptr || requestType == nullptr)
					return {DispatchStatus::NotFound};
				if (entry->requestWireBytes != requestType->wireBytes)
					return {};
				scratch = std::max(scratch, std::size_t(entry->scratchBytes));
				break;
			}
			case Operation::Service: {
				const auto* entry = model.services.find(item.id);
				const auto types = model.serviceTypeIds(item.id);
				requestType = types ? model.types.find(types->requestTypeId) : nullptr;
				responseType = types ? model.types.find(types->responseTypeId) : nullptr;
				if (entry == nullptr || requestType == nullptr || responseType == nullptr)
					return {DispatchStatus::NotFound};
				if (entry->requestWireBytes != requestType->wireBytes ||
				    entry->responseWireBytes != responseType->wireBytes)
					return {};
				scratch = std::max(scratch, std::size_t(entry->scratchBytes));
				break;
			}
			default:
				return {};
		}
		if (requestType == nullptr ? !item.payload.empty()
		                           : !validType(model.types, requestType->id, item.payload, true))
			return {};
		if (responseType != nullptr) {
			if (!validType(model.types, responseType->id))
				return {};
			if (responseType->wireBytes > batchMaxResponseBytes - responseNeeded)
				return {DispatchStatus::BufferTooSmall};
			responseNeeded += responseType->wireBytes;
			plans[i] = {responseType->id, responseType->wireBytes};
		}
	}
	if (responseNeeded > output.size())
		return {DispatchStatus::BufferTooSmall};
	if (workspace.used() > workspace.storage().size() ||
	    scratch > workspace.storage().size() - workspace.used())
		return {DispatchStatus::WorkspaceTooSmall};

	store(output, 0, static_cast<std::uint16_t>(items.size()));
	std::size_t cursor = 2;
	for (std::size_t i = 0; i < items.size(); ++i) {
		const auto& item = items[i];
		const auto destination = output.subspan(cursor + 12, plans[i].responseBytes);
		auto result =
		    dispatchEncoded(model, item.operation, item.id, item.payload, destination, workspace);
		if (!validResult(item.operation, result, plans[i].responseBytes) ||
		    (result.dispatch == DispatchStatus::Ok && result.endpointStatus == 0 &&
		     (item.operation == Operation::FieldRead || item.operation == Operation::Service) &&
		     !validType(model.types, plans[i].responseType, destination, true))) {
			result = {item.operation, DispatchStatus::InternalError};
		}
		store(output, cursor, item.id);
		output[cursor + 4] = std::byte(static_cast<std::uint8_t>(item.operation));
		output[cursor + 5] = std::byte(static_cast<std::uint8_t>(result.dispatch));
		output[cursor + 6] = std::byte{result.endpointStatus};
		output[cursor + 7] = std::byte{0};
		store(output, cursor + 8, static_cast<std::uint16_t>(result.written));
		output[cursor + 10] = output[cursor + 11] = std::byte{0};
		cursor += 12 + result.written;
	}
	return {DispatchStatus::Ok, static_cast<std::uint32_t>(cursor)};
}
} // namespace telemetry
