/* Encoded consumer: compiled boundaries, bounded scratch and resource reads.
 * Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT. */
#include "Fixture.hpp"
#include <algorithm>

namespace qualification {
// Explicit caller-owned buffers live outside the call chain. The encoded
// path must not introduce a second 4 KiB local object or hidden heap storage.
alignas(32) static std::array<std::byte, 16384> scratch;
constinit ts::Workspace sharedWorkspace{scratch};
static std::array<std::byte, 16384> output;
static std::array<std::byte, 4096> input;

extern "C" void consumer_encoded() noexcept
{
	using namespace fixture;
	auto& workspace = sharedWorkspace;
	ts::Workspace noScratch{std::span<std::byte>{}};
	const auto view = modelView();
	const auto small = std::span{output}.first(2);
	auto read = ts::readFieldEncoded(view, 1u, small, noScratch);
	check(read.dispatch == ts::DispatchStatus::Ok && read.written == 2);
	check(output[0] == std::byte{123} && output[1] == std::byte{0});
	check(noScratch.used() == 0);

	const auto priorReads = device.reads;
	check(ts::readFieldEncoded(view, 1u, small.first(1), workspace).dispatch ==
	      ts::DispatchStatus::BufferTooSmall);
	check(device.reads == priorReads);
	input[0] = std::byte{2};
	const auto priorWrites = device.writes;
	check(ts::writeFieldEncoded(view, 0u, std::span{input}.first(1), workspace).dispatch ==
	      ts::DispatchStatus::InvalidPayload);
	check(device.writes == priorWrites);

	const Config request{321, true};
	const auto requestBytes = std::span{input}.first(ts::wireSize<Config>);
	check(ts::encode(request, requestBytes) == ts::CodecStatus::Ok);
	const auto written = ts::writeFieldEncoded(view, 7u, requestBytes, workspace);
	check(written.dispatch == ts::DispatchStatus::Ok && written.endpointStatus == W::Applied);
	check(device.config.code == 321 && workspace.used() == 0);
	const auto command = ts::executeCommandEncoded(view, 0u, requestBytes, workspace);
	check(command.dispatch == ts::DispatchStatus::Ok && command.endpointStatus == C::Executed);
	const auto service = ts::callServiceEncoded(
	    view, 0u, requestBytes, std::span{output}.first(requestBytes.size()), workspace);
	check(service.dispatch == ts::DispatchStatus::Ok &&
	      service.endpointStatus == ts::ServiceStatus::Ok);
	check(service.written == requestBytes.size() && workspace.used() == 0);
	check(std::equal(requestBytes.begin(), requestBytes.end(), output.begin()));

	// A Service may decode the complete Request and then overwrite the same
	// wire buffer with its Response. This remains true across compiled TUs.
	const auto inPlace = ts::callServiceEncoded(view, 0u, requestBytes, requestBytes, workspace);
	check(inPlace.dispatch == ts::DispatchStatus::Ok && inPlace.written == requestBytes.size());

	const auto big = std::span{output}.first(ts::wireSize<Big>);
	read = ts::readFieldEncoded(view, 8u, big, workspace);
	check(read.dispatch == ts::DispatchStatus::Ok && read.written == 4096);
	check(output[0] == std::byte{1} && output[4] == std::byte{2} && output[8] == std::byte{3});
	check(workspace.used() == 0);
	const auto large = ts::callServiceEncoded(view, 2u, {}, big, workspace);
	check(large.dispatch == ts::DispatchStatus::Ok && large.written == 4096);
	check(workspace.used() == 0);
	const auto beforeLarge = device.services;
	check(ts::callServiceEncoded(view, 2u, {}, big, noScratch).dispatch ==
	      ts::DispatchStatus::WorkspaceTooSmall);
	check(device.services == beforeLarge);

	check(ts::readFieldEncoded(view, 9u, output, workspace).dispatch ==
	      ts::DispatchStatus::Unavailable);
	check(ts::executeCommandEncoded(view, 2u, requestBytes, workspace).dispatch ==
	      ts::DispatchStatus::Unavailable);
	check(ts::callServiceEncoded(view, 3u, requestBytes, output, workspace).dispatch ==
	      ts::DispatchStatus::Unavailable);
	check(ts::readFieldEncoded(view, 0x10000u, output, workspace).dispatch ==
	      ts::DispatchStatus::NotFound);

	const auto descriptor = descriptorBytes();
	check(descriptor.size() > 64 && descriptor[0] == std::byte{'T'});
	std::uint64_t stored = 0;
	for (unsigned i = 0; i < 8; ++i)
		stored |= std::uint64_t(std::to_integer<unsigned>(descriptor[16 + i])) << (8 * i);
	check(stored == fingerprint());
	const auto values = readValues(0, output);
	check(values.status == resource::Status::Ok && values.eof && values.written > 8192);
	check(output[0] == std::byte{'T'} && output[3] == std::byte{'3'});
	check(workspace.used() == 0);
}
} // namespace qualification
