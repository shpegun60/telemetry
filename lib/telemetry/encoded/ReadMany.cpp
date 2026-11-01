/*
 * @file ReadMany.cpp
 * @brief Metadata preflight and bounded staging for sequential encoded Field reads.
 * @author Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT
 */
#include <telemetry/encoded/ReadMany.hpp>
#include <algorithm>
#include <array>
#include <cstring>

namespace telemetry {
namespace {
template<class T>
void store(std::span<std::byte> output, std::size_t offset, T value) noexcept
{
	for (std::size_t i = 0; i < sizeof(T); ++i)
		output[offset + i] = std::byte((value >> (8 * i)) & 0xffu);
}

const TypeDescriptor* scalarType(ModelView model, const TypeDescriptor* type) noexcept
{
	if (type == nullptr)
		return nullptr;
	if (type->kind == TypeKind::Enum) {
		const auto* underlying = model.types.find(type->relatedTypeId);
		return underlying != nullptr && underlying->kind == TypeKind::Scalar &&
		               underlying->wireBytes == type->wireBytes
		           ? underlying
		           : nullptr;
	}
	return type->kind == TypeKind::Scalar ? type : nullptr;
}
} // namespace

ReadManyEncodedResult readManyEncoded(ModelView model, std::span<const PackedId> ids,
                                      std::span<std::byte> output, Workspace& workspace,
                                      detail::CurrentStructuredAbiTag,
                                      detail::CurrentReadManyEncodedLayoutTag) noexcept
{
	if (ids.empty() || ids.size() > readManyMaxItems)
		return {};
	for (std::size_t i = 0; i < ids.size(); ++i)
		for (std::size_t j = 0; j < i; ++j)
			if (ids[i] == ids[j])
				return {};
	if (buffersOverlap(std::as_bytes(ids), output) ||
	    buffersOverlap(std::as_bytes(ids), workspace.storage()) ||
	    buffersOverlap(output, workspace.storage()))
		return {};

	std::array<const FieldEntry*, readManyMaxItems> entries{};
	std::array<std::uint8_t, readManyMaxItems> lengths{};
	std::array<ReadManyItemStatus, readManyMaxItems> statuses{};
	std::array<bool, readManyMaxItems> bools{};
	std::size_t needed = 2 + 8 * ids.size(), scratch = 0;
	for (std::size_t i = 0; i < ids.size(); ++i) {
		const auto* entry = model.fields.find(ids[i]);
		if (entry == nullptr) {
			statuses[i] = ReadManyItemStatus::NotFound;
			continue;
		}
		const auto typeId = model.fieldTypeId(ids[i]);
		const auto* type = typeId ? model.types.find(*typeId) : nullptr;
		const auto* scalar = scalarType(model, type);
		if (scalar == nullptr || entry->wireBytes != type->wireBytes ||
		    (entry->wireBytes != 1 && entry->wireBytes != 2 && entry->wireBytes != 4 &&
		     entry->wireBytes != 8)) {
			statuses[i] = ReadManyItemStatus::UnsupportedType;
			continue;
		}
		entries[i] = entry;
		lengths[i] = static_cast<std::uint8_t>(entry->wireBytes);
		bools[i] = scalar->scalarCode == ScalarCode::Bool;
		needed += entry->wireBytes; // Hard ceiling: 2 + 32 * (8 + 8) = 514.
		scratch = std::max(scratch, static_cast<std::size_t>(entry->readScratchBytes));
	}
	if (needed > output.size())
		return {DispatchStatus::BufferTooSmall};
	if (workspace.used() > workspace.storage().size() ||
	    scratch > workspace.storage().size() - workspace.used())
		return {DispatchStatus::WorkspaceTooSmall};

	store(output, 0, static_cast<std::uint16_t>(ids.size()));
	std::size_t offset = 2;
	for (std::size_t i = 0; i < ids.size(); ++i) {
		std::array<std::byte, readManyMaxValueBytes> value{};
		if (entries[i] != nullptr) {
			const auto read =
			    entries[i]->readEncoded(std::span{value}.first(lengths[i]), workspace);
			if (read.dispatch != DispatchStatus::Ok || read.written != lengths[i] ||
			    (bools[i] && value[0] != std::byte{0} && value[0] != std::byte{1})) {
				statuses[i] = read.dispatch == DispatchStatus::Unavailable
				                  ? ReadManyItemStatus::Unavailable
				                  : ReadManyItemStatus::ReadError;
				lengths[i] = 0;
			}
		}
		store(output, offset, ids[i]);
		output[offset + 4] = std::byte(static_cast<std::uint8_t>(statuses[i]));
		output[offset + 5] = std::byte{0};
		store(output, offset + 6, static_cast<std::uint16_t>(lengths[i]));
		offset += 8;
		if (lengths[i] != 0)
			std::memcpy(output.data() + offset, value.data(), lengths[i]);
		offset += lengths[i];
	}
	return {DispatchStatus::Ok, static_cast<std::uint32_t>(offset)};
}
} // namespace telemetry
