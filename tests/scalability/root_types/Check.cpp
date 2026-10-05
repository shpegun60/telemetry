/*
 * @file Check.cpp
 * @brief Validate positional values and export canonical before/after evidence.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 *
 * Host-only fixture output is written in the runner's external working directory.
 * Type identity is compared through IDs and canonical bytes, never pointer values.
 */
#include "Fixture.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <span>

namespace f = root_types_fixture;
namespace ts = telemetry;
unsigned checks = 0, valueCases = 0;

// Count actual assertions; a failed condition cannot disappear with NDEBUG.
void check(bool condition)
{
	++checks;
	if (!condition) {
		std::fprintf(stderr, "root-types assertion %u failed\n", checks);
		std::exit(1);
	}
}

// Read an independently reconstructed canonical little-endian header member.
std::uint64_t little(std::span<const std::byte> bytes)
{
	std::uint64_t value = 0;
	for (std::size_t i = 0; i < bytes.size(); ++i)
		value |= std::uint64_t{std::to_integer<unsigned char>(bytes[i])} << (8 * i);
	return value;
}

// Persist exact wire bytes for comparison with the separately built baseline.
void save(const char* name, std::span<const std::byte> bytes)
{
	auto* file = std::fopen(name, "wb");
	check(file != nullptr);
	check(std::fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size());
	check(std::fclose(file) == 0);
}

std::array<std::byte, 67> valuesBytes()
{
	std::array<std::byte, 67> bytes{};
	f::device.reads.fill(0);
	const auto result = f::values.read(0, bytes);
	check(result.status == resource::Status::Ok && result.eof && result.next == bytes.size());
	check(result.written == bytes.size());
	for (auto count : f::device.reads)
		check(count == 1);
	check(little(std::span{bytes}.subspan(8, 4)) == 8);
	check(little(std::span{bytes}.subspan(12, 4)) == bytes.size());
	check(little(std::span{bytes}.subspan(16, 8)) == f::descriptor.fingerprint());
	check(bytes[0] == std::byte{'T'} && bytes[1] == std::byte{'V'} && bytes[2] == std::byte{'L'} &&
	      bytes[3] == std::byte{'3'});
	for (std::size_t i = 0; i < f::expectedTokens.size(); ++i)
		check(bytes[24 + i] == static_cast<std::byte>(f::expectedTokens[i]));
	return bytes;
}

// Every legal cursor is checked at several capacities; no-fit tokens must not
// sample getters. Token interior cursors remain invalid and never invoke owners.
void checkCursors(const std::array<std::byte, 67>& expected)
{
	for (std::uint32_t cursor = 0; cursor <= expected.size(); ++cursor) {
		const bool legal = cursor < 24 || std::find(f::offsets.begin(), f::offsets.end(), cursor) !=
		                                      f::offsets.end();
		for (std::size_t capacity : {0u, 1u, 2u, 3u, 6u, 7u, 8u, 16u, 32u, 80u}) {
			++valueCases;
			std::array<std::byte, 80> out;
			out.fill(std::byte{0xa5});
			f::device.reads.fill(0);
			const auto result = f::values.read(cursor, std::span{out}.first(capacity));
			std::size_t used = 0;
			if (legal && cursor < 24)
				used = std::min<std::size_t>(capacity, 24 - cursor);
			std::array<unsigned, 8> sampled{};
			if (legal) {
				for (std::size_t i = 0; i < sampled.size(); ++i) {
					if (f::offsets[i] != cursor + used)
						continue;
					const auto tokenBytes = f::offsets[i + 1] - f::offsets[i];
					if (tokenBytes > capacity - used)
						break;
					used += tokenBytes;
					sampled[i] = 1;
				}
			}
			const auto status = !legal ? resource::Status::InvalidCursor
			                    : used != 0 || cursor == expected.size()
			                        ? resource::Status::Ok
			                        : resource::Status::BufferTooSmall;
			check(result.status == status);
			check(result.written == used && result.next == cursor + used);
			check(result.eof == (legal && cursor + used == expected.size()));
			check(std::equal(out.begin(), out.begin() + used, expected.begin() + cursor));
			check(std::all_of(out.begin() + used, out.end(), [](auto byte) {
				return byte == std::byte{0xa5};
			}));
			check(f::device.reads == sampled);
			check(f::workspace.used() == 0);
		}
	}
	// Whole-file reads with several packet capacities must retain every token.
	for (std::size_t capacity : {7u, 8u, 11u, 24u, 67u}) {
		std::array<std::byte, 67> complete{};
		resource::Cursor cursor = 0;
		while (cursor != complete.size()) {
			const auto result = f::values.read(
			    cursor, std::span{complete}.subspan(
			                cursor, std::min<std::size_t>(capacity, complete.size() - cursor)));
			check(result.status == resource::Status::Ok && result.next > cursor);
			cursor = result.next;
		}
		check(complete == expected);
	}
}

// Registry metadata must map every declaration, including repeated payloads.
std::array<std::byte, 236> manifest()
{
	const auto view = f::model.view();
	std::array<std::uint32_t, 59> words{};
	std::size_t position = 0;
	const auto append = [&](std::uint32_t value) {
		check(position < words.size());
		words[position++] = value;
	};
	append(view.types.count);
	append(view.types.recordsBytes);
	for (auto id : {f::model.typeId<void>(), f::model.typeId<f::Leaf>(),
	                f::model.typeId<std::array<std::uint8_t, 3>>(), f::model.typeId<f::Packet>(),
	                f::model.typeId<f::SameShape>(), f::model.typeId<f::Mode>(),
	                f::model.typeId<f::Query>(), f::model.typeId<f::Reply>()})
		append(id);
	for (std::uint32_t group = 0; group < view.fields.count(); ++group) {
		for (std::uint32_t entry = 0; entry < view.fields.catalogs()[group].count; ++entry) {
			const auto id = (group << 16) | entry;
			append(id);
			check(view.fieldTypeId(id).has_value());
			append(*view.fieldTypeId(id));
		}
	}
	for (std::uint32_t group = 0; group < view.commands.count(); ++group) {
		for (std::uint32_t entry = 0; entry < view.commands.catalogs()[group].count; ++entry) {
			const auto id = (group << 16) | entry;
			append(id);
			check(view.commandTypeId(id).has_value());
			append(*view.commandTypeId(id));
		}
	}
	for (std::uint32_t group = 0; group < view.services.count(); ++group) {
		for (std::uint32_t entry = 0; entry < view.services.catalogs()[group].count; ++entry) {
			const auto id = (group << 16) | entry;
			append(id);
			check(view.serviceTypeIds(id).has_value());
			append(view.serviceTypeIds(id)->requestTypeId);
			append(view.serviceTypeIds(id)->responseTypeId);
		}
	}
	check(position == words.size());
	const auto legacy = f::legacyModel.view();
	for (std::uint32_t group = 0; group < view.fields.count(); ++group)
		for (std::uint32_t entry = 0; entry < view.fields.catalogs()[group].count; ++entry)
			check(legacy.fieldTypeId((group << 16) | entry) ==
			      view.fieldTypeId((group << 16) | entry));
	std::array<std::byte, 236> bytes{};
	for (std::size_t i = 0; i < words.size(); ++i)
		for (unsigned b = 0; b < 4; ++b)
			bytes[4 * i + b] = static_cast<std::byte>((words[i] >> (8 * b)) & 255u);
	return bytes;
}

// Owning and borrowed output paths share IDs while retaining their native form.
void checkOperations()
{
	const auto own = f::fieldA.read<0>();
	const auto borrow = f::fieldA.read<2>();
	check(own.has_value() && *own == f::device.a);
	check(borrow.hasValue() && borrow.valueOrNull() == &f::device.a);
	std::array<std::byte, 6> packet{};
	check(ts::encode(f::device.b, packet) == ts::CodecStatus::Ok);
	const auto written = ts::writeFieldEncoded(f::model.view(), 0, packet, f::workspace);
	check(written.dispatch == ts::DispatchStatus::Ok &&
	      written.endpointStatus == ts::WriteResult::Applied);
	check(f::device.a == f::device.b && f::device.writes == 1);
	for (auto id : {0u, 1u, 2u, 0x20000u, 0x20001u, 0x20002u}) {
		const auto* command = f::model.commandIndex().find(id);
		check(command != nullptr);
		const auto input = std::span{packet}.first(command->requestWireBytes);
		const auto result = ts::executeCommandEncoded(f::model.view(), id, input, f::workspace);
		check(result.dispatch == ts::DispatchStatus::Ok &&
		      result.endpointStatus == ts::CommandResult::Executed);
	}
	check(f::device.commands == 6);
	const auto ownService = f::serviceA.call<0>(f::device.b);
	const auto borrowedService = f::serviceA.call<3>(f::device.b);
	check(ownService.hasValue() && ownService.value() == f::device.b);
	check(borrowedService.hasValue() && borrowedService.valueOrNull() == &f::device.a);
	for (auto id : {0u, 1u, 2u, 3u, 4u, 0x20000u, 0x20001u}) {
		const auto* service = f::model.serviceIndex().find(id);
		check(service != nullptr);
		std::array<std::byte, 6> input{};
		std::array<std::byte, 6> output{};
		const auto result = ts::callServiceEncoded(
		    f::model.view(), id, std::span{input}.first(service->requestWireBytes),
		    std::span{output}.first(service->responseWireBytes), f::workspace);
		check(result.dispatch == ts::DispatchStatus::Ok &&
		      result.endpointStatus == ts::ServiceStatus::Ok);
		check(result.written == service->responseWireBytes && f::workspace.used() == 0);
	}
	check(f::device.services == 9);
}

int main()
{
	check(f::descriptor.valid() && f::model.types().count == 19);
	const auto values = valuesBytes();
	checkCursors(values);
	const auto identities = manifest();
	save("descriptor.bin", f::descriptorBytes);
	save("values.bin", values);
	save("type-ids.bin", identities);
	checkOperations();
	std::printf(
	    "{\"checks\":%u,\"value_read_cases\":%u,\"fields\":8,\"commands\":6,\"services\":7,\"type_count\":19,\"values_bytes\":67,\"descriptor_bytes\":%zu,\"fingerprint\":\"%016llx\"}\n",
	    checks, valueCases, f::descriptorBytes.size(),
	    static_cast<unsigned long long>(f::descriptor.fingerprint()));
}
