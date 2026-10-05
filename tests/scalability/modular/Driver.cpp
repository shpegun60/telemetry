/*
 * Functional, lifetime and bounded-peer proof for the modular fixture.
 *
 * The allocation counter covers automatic descriptor caching and every operation,
 * not C runtime startup. Two fixed peer contexts own separate scratch. No
 * hardware or asynchronous transport is modeled by this host program.
 * Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
 */
#include "Runtime.hpp"
#include "Clients.hpp"
#include <telemetry/model/Adapter.hpp>
#include <resource/FileSystem.hpp>
#include <structured_protocol/Bind.hpp>
#include <structured_protocol/Exchange.hpp>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <new>
#include <span>
#include <type_traits>

namespace {
std::size_t allocations = 0;
bool countAllocations = false;

// Public methods:
// - Peer(): Bind own scratch.
struct Peer {
	bool admitted = false;
	example::structured_protocol::Binding binding;
	std::array<std::byte, 32> scratch{};
	telemetry::Workspace workspace{scratch};

	Peer() noexcept = default;
};

static_assert(!std::is_copy_constructible_v<Peer> && !std::is_move_constructible_v<Peer>);

void put(std::span<std::byte> output, std::size_t offset, std::uint64_t value,
         unsigned bytes) noexcept
{
	for (unsigned i = 0; i < bytes; ++i) {
		output[offset + i] = std::byte((value >> (i * 8)) & 0xffu);
	}
}

void magic(std::span<std::byte> output, const char* text) noexcept
{
	for (unsigned i = 0; i < 4; ++i) {
		output[i] = std::byte(static_cast<unsigned char>(text[i]));
	}
}

std::uint32_t get32(std::span<const std::byte> input) noexcept
{
	std::uint32_t result = 0;
	for (unsigned i = 0; i < 4; ++i) {
		result |= std::uint32_t(std::to_integer<unsigned char>(input[i])) << (i * 8);
	}
	return result;
}
} // namespace

void* operator new(std::size_t size)
{
	if (countAllocations) {
		++allocations;
	}
	if (auto* result = std::malloc(size == 0 ? 1 : size)) {
		return result;
	}
	std::abort();
}

void* operator new[](std::size_t size)
{
	return ::operator new(size);
}

void operator delete(void* object) noexcept
{
	std::free(object);
}

void operator delete[](void* object) noexcept
{
	std::free(object);
}

void operator delete(void* object, std::size_t) noexcept
{
	std::free(object);
}

void operator delete[](void* object, std::size_t) noexcept
{
	std::free(object);
}

int main()
{
	countAllocations = true;
	const auto expectedFingerprint = modular::fingerprint();
	const auto& model = modular::modelView();
	assert(&modular::modelView() == &model);
	assert(model.types.count == 17 && modular::typeCount() == 17);
	assert(model.fields.count() == 3 && model.commands.count() == 3 && model.services.count() == 3);
	assert(model.fieldTypeId(0) != model.fieldTypeId(1u << 16));
	assert(model.fieldTypeId(1u << 16) != model.fieldTypeId(2u << 16));
	assert(model.commandTypeId(0) == model.commandTypeId(1u << 16));
	assert(model.serviceTypeIds(0)->requestTypeId == model.serviceTypeIds(2u << 16)->requestTypeId);
	assert(model.serviceTypeIds(0)->responseTypeId == *model.fieldTypeId(0));

	for (const auto client : modular::clients) {
		assert(client());
	}

	const auto files = modular::files();
	assert(files.size() == 1 && files[0].readable() && !files[0].writable());
	std::array<std::byte, 4> version{};
	const auto read = files[0].read(0, version);
	assert(read.status == resource::Status::Ok && read.eof && read.written == 4);
	assert(version[0] == std::byte{1} && version[3] == std::byte{4});

	// Admission capacity counts both unbound and bound peers.
	std::array<Peer, 2> peers{};
	auto admit = [&]() noexcept -> Peer* {
		for (auto& peer : peers) {
			if (!peer.admitted) {
				peer.admitted = true;
				return &peer;
			}
		}
		return nullptr;
	};
	auto* first = admit();
	auto* second = admit();
	assert(first != nullptr && second != nullptr && first != second && admit() == nullptr);
	std::array<std::byte, 16> bindRequest{};
	std::array<std::byte, 32> reply{};
	magic(bindRequest, "TSBN");
	put(bindRequest, 4, 3, 2);
	put(bindRequest, 6, 0, 2);
	put(bindRequest, 8, expectedFingerprint, 8);
	for (auto* peer : {first, second}) {
		const auto bind = example::structured_protocol::Bind::process(
		    peer->binding, model, expectedFingerprint, bindRequest, reply);
		assert(bind.dispatch == example::structured_protocol::PacketStatus::Ok &&
		       peer->binding.ready());
	}

	std::array<std::byte, 28> commandRequest{};
	magic(commandRequest, "TSRQ");
	put(commandRequest, 4, 3, 2);
	put(commandRequest, 6, 0, 2);
	put(commandRequest, 8, 1, 4);         // Request correlation.
	put(commandRequest, 12, 1u << 16, 4); // Module B, local command zero.
	put(commandRequest, 16, 4, 4);
	commandRequest[20] = std::byte{2}; // Command.
	put(commandRequest, 24, 1, 4);
	std::array<std::byte, 4> before{}, after{};
	const auto prior = telemetry::readFieldEncoded(model, 1u << 16, before, first->workspace);
	assert(prior.dispatch == telemetry::DispatchStatus::Ok && prior.written == before.size());
	const auto exchanged = example::structured_protocol::Exchange::process(
	    first->binding, commandRequest, reply, first->workspace);
	assert(exchanged.dispatch == example::structured_protocol::PacketStatus::Ok &&
	       exchanged.written == 24 && first->workspace.used() == 0);
	const auto changed = telemetry::readFieldEncoded(model, 1u << 16, after, first->workspace);
	assert(changed.dispatch == telemetry::DispatchStatus::Ok && changed.written == after.size() &&
	       get32(after) == get32(before) + 1);
	first->binding.reset();
	first->admitted = false;
	assert(!first->binding.ready() && second->binding.ready() && admit() == first);
	const auto rejected = example::structured_protocol::Exchange::process(
	    first->binding, commandRequest, reply, first->workspace);
	assert(rejected.dispatch == example::structured_protocol::PacketStatus::NotReady);

	countAllocations = false;
	assert(allocations == 0);
	std::printf("Modular fixture: clients=%zu types=%u families=3 peers=2 allocations=%zu\n",
	            modular::clients.size(), modular::typeCount(), allocations);
}
