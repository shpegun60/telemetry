/*
 * @file Check.cpp
 * @brief Counted typed-client protocol, replay, lifetime and storage checks.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#include <client/rpc/RpcClient.hpp>
#include "../rpc/Fixture.hpp"
#include <resource/protocol/Protocol.hpp>
#include <resource/protocol/Packets.hpp>
#include <cstdio>

using namespace rpc_fixture;
using Client = ::telemetry::client::rpc::RpcClient;
using CS = ::telemetry::client::rpc::RpcClientStatus;
using ClientState = ::telemetry::client::rpc::RpcClientState;
namespace pc = resource::protocol::client;
static unsigned checks = 0, failures = 0;

static void check(bool condition, unsigned line) noexcept
{
	++checks;
	if (!condition) {
		++failures;
		std::fprintf(stderr, "rpc_client Check.cpp:%u failed (%u)\n", line, checks);
	}
}

#define CHECK(value) check(bool(value), __LINE__)

inline constexpr ::telemetry::client::rpc::FieldEndpoint<Config> configField{
    0, ::telemetry::client::rpc::trustedRpcMapping};
inline constexpr ::telemetry::client::rpc::FieldEndpoint<Big> bigField{
    3, ::telemetry::client::rpc::trustedRpcMapping};
inline constexpr ::telemetry::client::rpc::CommandEndpoint<Config> configure{
    0, ::telemetry::client::rpc::trustedRpcMapping};
inline constexpr ::telemetry::client::rpc::CommandEndpoint<> resetCommand{
    1, ::telemetry::client::rpc::trustedRpcMapping};
inline constexpr ::telemetry::client::rpc::ServiceEndpoint<Config, Config> echo{
    0, ::telemetry::client::rpc::trustedRpcMapping};
inline constexpr ::telemetry::client::rpc::ServiceEndpoint<void, void> ping{
    1, ::telemetry::client::rpc::trustedRpcMapping};
inline constexpr ::telemetry::client::rpc::ServiceEndpoint<Config, Big> large{
    3, ::telemetry::client::rpc::trustedRpcMapping};

template<std::size_t Packet = 64, std::size_t Result = ts::wireSize<Big>>
struct ClientHarness {
	Guarded<24 + ts::wireSize<Big>> request;
	Guarded<Result> result;
	Guarded<Packet> packet;
	Client client{0, request.output(), result.output(), packet.output(), {5, 1000, 2, 2}};
	Harness<> server;
	resource::FileSystem<1> files{std::array{resource::file("/rpc", server.mailbox)}};
	std::array<std::byte, 97> response{};
	std::uint32_t now = 0;

	ClientHarness() noexcept
	{
		CHECK(client.startSession(1) == CS::Ok);
	}

	auto receive(::telemetry::client::rpc::RpcClientPacket pending) noexcept
	{
		CHECK(bool(pending));
		CHECK(pending.generation == 1);
		CHECK(client.onSendAccepted(pending.generation, pending.requestId));
		const auto responseResult =
		    resource::protocol::process(files.view(), pending.bytes, response);
		CHECK(responseResult.status == R::Ok);
		CHECK(client.onReply(Input{response}.first(responseResult.written), pending.generation,
		                     pending.requestId));
		return responseResult;
	}

	void run(bool dispatch = true) noexcept
	{
		for (unsigned steps = 0; client.active() && steps != 200; ++steps) {
			client.process(now++);
			const auto pending = client.nextPacket();
			if (pending)
				receive(pending);
			if (dispatch)
				server.mailbox.process(now);
		}
		CHECK(client.completed());
		CHECK(request.intact() && result.intact() && packet.intact() && server.intact());
	}
};

static void operations() noexcept
{
	device = {};
	ClientHarness<> h;
	Config request{731, false};
	CHECK(h.client.beginService(echo, request) == CS::Ok);
	CHECK(h.client.beginReadField(configField) == CS::Busy);
	request = {999, true};
	h.run();
	Config output{1, true};
	CHECK(h.client.decodeResult(output).status == CS::Ok);
	CHECK(output.value == 731 && !output.enabled && device.services == 1);
	std::uint32_t wrong = 0x87654321u;
	CHECK(h.client.decodeResult(wrong).status == CS::WrongType && wrong == 0x87654321u);

	struct Other {
		std::uint32_t value;
		bool enabled;
	} sameSize{};

	CHECK(ts::wireSize<Other> == ts::wireSize<Config>);
	CHECK(h.client.decodeResult(sameSize).status == CS::WrongType);
	const auto previous = h.client.resultBytes();
	std::array<std::byte, 5> retained{};
	std::copy(previous.begin(), previous.end(), retained.begin());
	CHECK(h.client.startSession(1) == CS::InvalidGeneration);
	CHECK(h.client.resultBytes().size() == 5 &&
	      std::equal(retained.begin(), retained.end(), previous.begin()));
	CHECK(h.client.beginCommand(resetCommand) == CS::Ok);
	h.run();
	CHECK(device.commands == 1);
	CHECK(h.client.beginWriteField(configField, Config{43, true}) == CS::Ok);
	h.run();
	CHECK(device.current.value == 43 && device.writes == 1);
	CHECK(h.client.beginReadField(configField) == CS::Ok);
	h.run();
	CHECK(h.client.decodeResult(output).status == CS::Ok && output.value == 43);
	CHECK(h.client.beginCommand(configure, Config{92, false}) == CS::Ok);
	h.run();
	CHECK(device.current.value == 92 && device.commands == 2);
	CHECK(h.client.beginService(ping) == CS::Ok);
	h.run();
	CHECK(h.client.resultBytes().empty());
	CHECK(h.client.beginService(large, Config{72, true}) == CS::Ok);
	h.run();
	static Big bigOutput{};
	CHECK(h.client.decodeResult(bigOutput).status == CS::Ok && bigOutput.words.front() == 72);
	static std::array<std::byte, ts::scratchBytes<Big>> scratch{};
	ts::Workspace workspace{scratch};
	auto lease = workspace.reserve<Big>();
	Big* pointer = nullptr;
	CHECK(h.client.decodeResult(lease, pointer).status == CS::Ok);
	CHECK(pointer != nullptr && pointer->words.front() == 72);
	CHECK(h.client.beginReadField(bigField) == CS::Ok);
	h.run();
	CHECK(h.client.decodeResult(bigOutput).status == CS::Ok);
	CHECK(bigOutput.words.front() == 72);
	CHECK(h.client.status() == CS::Ok && h.client.diagnostic().codec == ts::CodecStatus::Ok);
	h.client.reset();
	CHECK(h.client.state() == ClientState::Idle && h.client.resultBytes().empty());
}

static void statuses() noexcept
{
	for (unsigned status = 0; status <= unsigned(C::Failed); ++status) {
		device.commandStatus = static_cast<C>(status);
		ClientHarness<> h;
		CHECK(h.client.beginCommand(resetCommand) == CS::Ok);
		h.run();
		CHECK(h.client.diagnostic().endpointStatus == status);
		CHECK(h.client.diagnostic().dispatch == ts::DispatchStatus::Ok);
		CHECK(h.client.diagnostic().operation == Op::Command);
	}
	device.commandStatus = C::Executed;
	for (unsigned status = 0; status <= unsigned(W::Unavailable); ++status) {
		device.writeStatus = static_cast<W>(status);
		ClientHarness<> h;
		CHECK(h.client.beginWriteField(configField, Config{1, true}) == CS::Ok);
		h.run();
		CHECK(h.client.diagnostic().endpointStatus == status);
	}
	device.writeStatus = W::Applied;
	for (unsigned status = 0; status <= unsigned(S::Failed); ++status) {
		device.serviceStatus = static_cast<S>(status);
		ClientHarness<> h;
		CHECK(h.client.beginService(echo, Config{22, true}) == CS::Ok);
		h.run();
		CHECK(h.client.diagnostic().endpointStatus == status);
		Config value{};
		CHECK(h.client.decodeResult(value).status == (status ? CS::ResultUnavailable : CS::Ok));
	}
	device.serviceStatus = S::Ok;
	ClientHarness<> h;
	CHECK(h.client.beginCommand(::telemetry::client::rpc::CommandEndpoint<>{
	          0xffffu, ::telemetry::client::rpc::trustedRpcMapping}) == CS::Ok);
	h.run();
	CHECK(h.client.diagnostic().dispatch == ts::DispatchStatus::NotFound);
	CHECK(h.client.diagnostic().endpointStatus == 0);
}

static void retriesAndPending() noexcept
{
	device = {};
	ClientHarness<40> h;
	CHECK(h.client.beginService(echo, Config{222, true}) == CS::Ok);
	auto packet = h.client.nextPacket();
	CHECK(packet.bytes.size() == 40);
	std::array<std::byte, 40> stable{};
	std::copy(packet.bytes.begin(), packet.bytes.end(), stable.begin());
	CHECK(!h.client.onReply({}, packet.generation, packet.requestId));
	CHECK(h.client.onSendAccepted(packet.generation, packet.requestId));
	const auto first = resource::protocol::process(h.files.view(), packet.bytes, h.response);
	CHECK(first.status == R::Ok && device.services == 0);
	CHECK(!h.client.nextPacket());
	h.client.process(5);
	const auto repeat = h.client.nextPacket();
	CHECK(repeat.requestId == packet.requestId && repeat.bytes.size() == packet.bytes.size());
	CHECK(std::equal(stable.begin(), stable.end(), repeat.bytes.begin()));
	CHECK(h.client.onSendAccepted(repeat.generation, repeat.requestId));
	const auto repeatedAck = resource::protocol::process(h.files.view(), repeat.bytes, h.response);
	CHECK(h.client.onReply(Input{h.response}.first(repeatedAck.written), repeat.generation,
	                       repeat.requestId));
	CHECK(!h.client.onReply(Input{h.response}.first(repeatedAck.written), repeat.generation,
	                        repeat.requestId));
	packet = h.client.nextPacket();
	CHECK(packet.bytes[13] == std::byte{1});
	CHECK(pc::detail::load<resource::Cursor>(packet.bytes, 5) ==
	      ::telemetry::protocol::rpc::rpcCursor(1, 24));
	CHECK(h.client.onSendAccepted(packet.generation, packet.requestId));
	const auto finalAck = resource::protocol::process(h.files.view(), packet.bytes, h.response);
	CHECK(finalAck.status == R::Ok && h.server.mailbox.state() == State::Pending);
	h.server.mailbox.process(6);
	CHECK(device.services == 1);
	h.client.process(10);
	const auto finalRetry = h.client.nextPacket();
	CHECK(finalRetry.requestId == packet.requestId);
	CHECK(h.client.onSendAccepted(finalRetry.generation, finalRetry.requestId));
	const auto recovered =
	    resource::protocol::process(h.files.view(), finalRetry.bytes, h.response);
	CHECK(h.client.onReply(Input{h.response}.first(recovered.written), finalRetry.generation,
	                       finalRetry.requestId));
	CHECK(device.services == 1);
	packet = h.client.nextPacket();
	CHECK(packet.bytes[0] == std::byte(resource::protocol::Op::Read));
	CHECK(pc::detail::load<resource::Cursor>(packet.bytes, 5) == 0);
	h.receive(packet);
	CHECK(h.client.completed() && device.services == 1);
	Config result{};
	CHECK(h.client.decodeResult(result).status == CS::Ok && result.value == 222);

	ClientHarness<> pending;
	CHECK(pending.client.beginService(ping) == CS::Ok);
	pending.receive(pending.client.nextPacket());
	pending.receive(pending.client.nextPacket());
	CHECK(pending.client.state() == ClientState::Polling && !pending.client.completed());
	CHECK(!pending.client.nextPacket());
	pending.client.process(2);
	CHECK(pending.client.nextPacket());
	pending.server.mailbox.process(2);
	pending.receive(pending.client.nextPacket());
	CHECK(pending.client.completed());
}

static void deadlinesAndGeneration() noexcept
{
	ClientHarness<> h;
	CHECK(h.client.beginCommand(resetCommand) == CS::Ok);
	h.client.process(6);
	CHECK(h.client.status() == CS::Ok); // No handoff, no ACK deadline.
	auto p = h.client.nextPacket();
	CHECK(!h.client.onSendAccepted(2, p.requestId));
	CHECK(h.client.onSendAccepted(1, p.requestId));
	CHECK(!h.client.onReply({}, 2, p.requestId));
	CHECK(!h.client.onReply({}, 1, p.requestId + 1));
	h.client.loseSession(2);
	CHECK(h.client.active());
	h.client.loseSession(1);
	CHECK(h.client.status() == CS::OutcomeUnknown &&
	      h.client.diagnostic().cause == CS::SessionLost);
	CHECK(h.client.startSession(1) == CS::InvalidGeneration);
	CHECK(h.client.status() == CS::OutcomeUnknown);
	CHECK(h.client.startSession(2, UINT32_MAX - 1) == CS::Ok);
	CHECK(h.client.beginCommand(resetCommand) == CS::Ok);
	p = h.client.nextPacket();
	CHECK(pc::detail::load<std::uint32_t>(p.bytes, 24) == UINT32_MAX);
	h.client.reset();
	CHECK(h.client.status() == CS::Cancelled);
	CHECK(h.client.beginCommand(resetCommand) == CS::TransactionIdsExhausted);
	CHECK(h.client.startSession(3) == CS::Ok);
	CHECK(!h.client.onReply({}, 1, p.requestId));

	ClientHarness<40> partial;
	CHECK(partial.client.beginService(echo, Config{1, true}) == CS::Ok);
	p = partial.client.nextPacket();
	CHECK(partial.client.onSendAccepted(1, p.requestId));
	partial.client.loseSession(1);
	CHECK(partial.client.status() == CS::SessionLost);

	for (bool final : {false, true}) {
		ClientHarness<40> timeout;
		if (final)
			CHECK(timeout.client.beginCommand(resetCommand) == CS::Ok);
		else
			CHECK(timeout.client.beginService(echo, Config{1, true}) == CS::Ok);
		for (unsigned attempt = 0; attempt < 3; ++attempt) {
			p = timeout.client.nextPacket();
			CHECK(bool(p));
			CHECK(timeout.client.onSendAccepted(1, p.requestId));
			timeout.client.process((attempt + 1) * 5);
		}
		CHECK(timeout.client.status() == (final ? CS::OutcomeUnknown : CS::Timeout));
		CHECK(timeout.client.diagnostic().cause == CS::Timeout);
	}
	ClientHarness<> wrap;
	wrap.client.process(UINT32_MAX - 2);
	CHECK(wrap.client.beginCommand(resetCommand) == CS::Ok);
	p = wrap.client.nextPacket();
	CHECK(wrap.client.onSendAccepted(1, p.requestId));
	wrap.client.process(2);
	CHECK(wrap.client.nextPacket().requestId == p.requestId);
	wrap.client.process(1000);
	CHECK(wrap.client.status() == CS::OutcomeUnknown);
}

static void malformedAndStorage() noexcept
{
	// Mutate each meaningful header field, including holes in DispatchStatus.
	for (unsigned byte : {0u, 4u, 6u, 8u, 12u, 16u, 20u, 21u, 22u, 23u, 24u, 28u}) {
		ClientHarness<> h;
		CHECK(h.client.beginService(echo, Config{2, true}) == CS::Ok);
		h.receive(h.client.nextPacket());
		h.server.mailbox.process(0);
		const auto p = h.client.nextPacket();
		CHECK(h.client.onSendAccepted(1, p.requestId));
		const auto response = resource::protocol::process(h.files.view(), p.bytes, h.response);
		h.response[12 + byte] = std::byte{0xff};
		CHECK(h.client.onReply(Input{h.response}.first(response.written), 1, p.requestId));
		CHECK(h.client.status() == CS::OutcomeUnknown &&
		      h.client.diagnostic().cause == CS::MalformedReply);
		Config output{12345, false};
		CHECK(h.client.decodeResult(output).status == CS::ResultUnavailable &&
		      output.value == 12345);
	}
	for (unsigned byte : {0u, 1u, 9u, 10u, 13u}) {
		ClientHarness<> h;
		CHECK(h.client.beginCommand(resetCommand) == CS::Ok);
		const auto p = h.client.nextPacket();
		CHECK(h.client.onSendAccepted(1, p.requestId));
		const auto response = resource::protocol::process(h.files.view(), p.bytes, h.response);
		h.response[byte] = std::byte{0xff};
		CHECK(h.client.onReply(Input{h.response}.first(response.written), 1, p.requestId));
		CHECK(h.client.status() == CS::OutcomeUnknown);
	}
	ClientHarness<> codec;
	CHECK(codec.client.beginService(echo, Config{5, true}) == CS::Ok);
	codec.receive(codec.client.nextPacket());
	codec.server.mailbox.process(0);
	const auto p = codec.client.nextPacket();
	CHECK(codec.client.onSendAccepted(1, p.requestId));
	const auto response = resource::protocol::process(codec.files.view(), p.bytes, codec.response);
	codec.response[12 + 32 + 4] = std::byte{2};
	CHECK(codec.client.onReply(Input{codec.response}.first(response.written), 1, p.requestId));
	CHECK(codec.client.completed());
	Config output{843, false};
	CHECK(codec.client.decodeResult(output).status == CS::CodecError);
	CHECK(output.value == 843 && !output.enabled);
	CHECK(codec.client.diagnostic().codec == ts::CodecStatus::InvalidValue);

	ClientHarness<64, 5> small;
	CHECK(small.client.beginService(echo, Config{88, true}) == CS::Ok);
	small.run();
	const auto before = small.client.diagnostic();
	const auto retained = small.result.bytes;
	CHECK(small.client.beginService(large, Config{88, true}) == CS::BufferTooSmall);
	CHECK(small.client.completed() && small.result.bytes == retained);
	CHECK(small.client.diagnostic().codec == before.codec &&
	      small.client.diagnostic().dispatch == before.dispatch);
	// Begin rejects sources anywhere in its storage, before mutating evidence.
	alignas(Config) std::array<std::byte, 128> requestStorage{};
	std::array<std::byte, 16> resultStorage{};
	std::array<std::byte, 64> packetStorage{};
	Client alias{0, requestStorage, resultStorage, packetStorage};
	CHECK(alias.startSession(1) == CS::Ok);
	Config* source = ::new (requestStorage.data()) Config{1, true};
	const auto bytes = requestStorage;
	CHECK(alias.beginWriteField(configField, *source) == CS::Overlap);
	CHECK(requestStorage == bytes);
	std::destroy_at(source);
	CHECK(!std::is_copy_constructible_v<Client> && !std::is_move_constructible_v<Client>);
}

static void largeRequest() noexcept
{
	struct Huge {
		std::array<std::uint32_t, 17500> bytes;
	};

	static Huge value{};
	static std::array<std::byte, 70024> request{};
	static std::array<std::byte, 70024> captured{};
	static std::array<std::byte, 65551> outgoing{};
	std::array<std::byte, 1> result{};
	Client client{0, request, result, outgoing};
	CHECK(client.startSession(1) == CS::Ok);
	value.bytes.front() = 31;
	value.bytes.back() = 74;
	CHECK(client.beginCommand(
	          ::telemetry::client::rpc::CommandEndpoint<Huge>{
	              0, ::telemetry::client::rpc::trustedRpcMapping},
	          value) == CS::Ok);
	value.bytes.front() = 99;
	std::uint32_t offset = 0;
	for (unsigned fragment = 0; fragment != 2; ++fragment) {
		const auto p = client.nextPacket();
		CHECK(bool(p));
		const auto count = pc::detail::load<std::uint16_t>(p.bytes, 14);
		CHECK(count == (fragment == 0 ? 65535 : 4489));
		CHECK(p.bytes[13] == std::byte(fragment == 1));
		CHECK(pc::detail::load<resource::Cursor>(p.bytes, 5) ==
		      (fragment == 0 ? 0 : ::telemetry::protocol::rpc::rpcCursor(1, offset)));
		std::copy_n(p.bytes.begin() + 16, count, captured.begin() + offset);
		CHECK(client.onSendAccepted(1, p.requestId));
		offset += count;
		std::array<std::byte, 14> ack{};
		pc::detail::store(Output{ack}, 1, ::telemetry::protocol::rpc::rpcCursor(1, offset));
		pc::detail::store(Output{ack}, 9, std::uint32_t(count));
		ack[13] = std::byte(fragment == 1);
		CHECK(client.onReply(ack, 1, p.requestId));
	}
	CHECK(offset == 70024 && captured[24] == std::byte{31} && captured[70020] == std::byte{74});
	CHECK(client.nextPacket().bytes.size() == 13);
}

static void readRetryAndRepresentation() noexcept
{
	ClientHarness<> h;
	CHECK(h.client.beginService(large, Config{881, true}) == CS::Ok);
	h.receive(h.client.nextPacket());
	h.server.mailbox.process(0);
	h.receive(h.client.nextPacket());
	CHECK(h.client.active() && h.client.resultBytes().empty());
	const auto p = h.client.nextPacket();
	std::array<std::byte, 13> stable{};
	std::copy(p.bytes.begin(), p.bytes.end(), stable.begin());
	CHECK(h.client.onSendAccepted(1, p.requestId));
	const auto reply = resource::protocol::process(h.files.view(), p.bytes, h.response);
	CHECK(reply.status == R::Ok);
	h.client.process(5);
	const auto retry = h.client.nextPacket();
	CHECK(retry.requestId == p.requestId &&
	      std::equal(stable.begin(), stable.end(), retry.bytes.begin()));
	// A delayed answer to the accepted original is valid even before the
	// scheduled retry is handed off, since both name exactly the same read.
	CHECK(h.client.onReply(Input{h.response}.first(reply.written), 1, p.requestId));
	CHECK(!h.client.onSendAccepted(1, retry.requestId));
	CHECK(!h.client.onReply(Input{h.response}.first(reply.written), 1, p.requestId));
	h.now = 5;
	h.run();
	static Big out;
	CHECK(h.client.decodeResult(out).status == CS::Ok && out.words.front() == 881);
	CHECK(out.words.back() == 0);
	std::array<std::byte, 8> tinyScratch{};
	ts::Workspace tooSmall{tinyScratch};
	auto lease = tooSmall.reserve<Big>();
	Big* pointer = nullptr;
	const auto smallDecode = h.client.decodeResult(lease, pointer);
	CHECK(smallDecode.status == CS::CodecError &&
	      smallDecode.codec == ts::CodecStatus::WorkspaceTooSmall);
	CHECK(pointer == nullptr && h.client.completed());
	CHECK(h.client.decodeResult(out).status == CS::Ok);

	// A scalar field tests exact local type identity independent of wire size.
	// This simulated remote reply carries the existing canonical Codec bytes.
	ClientHarness<> scalar;
	constexpr ::telemetry::client::rpc::FieldEndpoint<float> floatField{
	    0, ::telemetry::client::rpc::trustedRpcMapping};
	CHECK(scalar.client.beginReadField(floatField) == CS::Ok);
	scalar.receive(scalar.client.nextPacket());
	const auto read = scalar.client.nextPacket();
	CHECK(scalar.client.onSendAccepted(1, read.requestId));
	std::array<std::byte, 48> encoded{};
	pc::detail::store(Output{encoded}, 1, ::telemetry::protocol::rpc::rpcCursor(1, 36));
	encoded[9] = std::byte{1};
	pc::detail::store(Output{encoded}, 10, std::uint16_t{36});
	pc::detail::store(Output{encoded}, 12, std::uint32_t{0x53505254});
	pc::detail::store(Output{encoded}, 16, std::uint16_t{1});
	pc::detail::store(Output{encoded}, 20, std::uint32_t{1});
	pc::detail::store(Output{encoded}, 28, std::uint32_t{4});
	encoded[32] = std::byte(Op::FieldRead);
	encoded[33] = std::byte(State::Ready);
	pc::detail::store(Output{encoded}, 36, std::uint32_t{24});
	pc::detail::store(Output{encoded}, 40, std::uint32_t{24});
	CHECK(ts::encode(3.5f, Output{encoded}.subspan(44)) == ts::CodecStatus::Ok);
	CHECK(scalar.client.onReply(encoded, 1, read.requestId));
	std::uint32_t wrong = 123;
	float value = 0;
	CHECK(scalar.client.decodeResult(wrong).status == CS::WrongType && wrong == 123);
	CHECK(scalar.client.decodeResult(value).status == CS::Ok && value == 3.5f);
}

static void cursorAndEnvelopeChecks() noexcept
{
	// READ envelope cursor, EOF, count and zero progress are not trusted.
	for (unsigned mutation = 0; mutation != 8; ++mutation) {
		ClientHarness<> h;
		CHECK(h.client.beginService(large, Config{1, true}) == CS::Ok);
		h.receive(h.client.nextPacket());
		h.server.mailbox.process(0);
		if (mutation >= 4)
			h.receive(h.client.nextPacket());
		const auto p = h.client.nextPacket();
		CHECK(h.client.onSendAccepted(1, p.requestId));
		const auto response = resource::protocol::process(h.files.view(), p.bytes, h.response);
		switch (mutation % 4) {
			case 0:
				h.response[1] ^= std::byte{1};
				break;
			case 1:
				h.response[9] = std::byte{1};
				break;
			case 2:
				h.response[10] ^= std::byte{1};
				break;
			case 3:
				h.response[0] = std::byte{127};
				break;
		}
		CHECK(h.client.onReply(Input{h.response}.first(response.written), 1, p.requestId));
		CHECK(h.client.status() == CS::OutcomeUnknown);
		CHECK(h.client.resultBytes().empty());
	}
	// Valid Resource error envelopes keep their original provider status.
	for (unsigned status = 1; status <= unsigned(R::InternalError); ++status) {
		ClientHarness<> h;
		CHECK(h.client.beginCommand(resetCommand) == CS::Ok);
		const auto p = h.client.nextPacket();
		CHECK(h.client.onSendAccepted(1, p.requestId));
		std::array<std::byte, 14> error{};
		error[0] = std::byte(status);
		CHECK(h.client.onReply(error, 1, p.requestId));
		CHECK(h.client.diagnostic().resourceStatus == static_cast<R>(status));
		CHECK(h.client.diagnostic().cause == CS::ResourceError);
	}
	ClientHarness<> shortReply;
	CHECK(shortReply.client.beginCommand(resetCommand) == CS::Ok);
	const auto p = shortReply.client.nextPacket();
	CHECK(shortReply.client.onSendAccepted(1, p.requestId));
	const std::array malformed{std::byte(R::InvalidData)};
	CHECK(shortReply.client.onReply(malformed, 1, p.requestId));
	CHECK(shortReply.client.diagnostic().resourceStatus == R::InvalidData);
	CHECK(shortReply.client.diagnostic().cause == CS::ResourceError);
}

static void enumArraysAndAgreement() noexcept
{
	enum class Mode : std::uint16_t {
		Off = 0,
		On = 7
	};

	struct Typed {
		Mode mode;
		std::array<std::int16_t, 3> values;
	};

	constexpr ::telemetry::client::rpc::CommandEndpoint<Typed> command{
	    0, ::telemetry::client::rpc::trustedRpcMapping};
	std::array<std::byte, 32> request{};
	std::array<std::byte, 1> result{};
	std::array<std::byte, 48> outgoing{};
	Client client{0, request, result, outgoing};
	CHECK(client.beginCommand(command, Typed{}) == CS::NoSession);
	// This is an application handshake simulation, not a descriptor parser.
	// A deployment obtains the remote value through its admitted transport.
	const auto admit = [&](std::uint64_t expected, std::uint64_t remote) {
		return expected == remote ? client.startSession(1) : CS::NoSession;
	};
	CHECK(admit(0x12345678u, 0x12345679u) == CS::NoSession);
	CHECK(client.beginCommand(command, Typed{}) == CS::NoSession);
	CHECK(admit(0x12345678u, 0x12345678u) == CS::Ok);
	Typed value{Mode::On, {-3, 41, -8}};
	CHECK(client.beginCommand(command, value) == CS::Ok);
	value.values.fill(99);
	const auto p = client.nextPacket();
	CHECK(p.bytes.size() == 48 && p.bytes[40] == std::byte{7} && p.bytes[41] == std::byte{0});
	CHECK(p.bytes[42] == std::byte{0xfd} && p.bytes[43] == std::byte{0xff});
	CHECK(p.bytes[44] == std::byte{41} && p.bytes[46] == std::byte{0xf8});
}

static void headerOnlyReady() noexcept
{
	for (bool zeroProgress : {false, true}) {
		ClientHarness<> h;
		CHECK(h.client.beginService(echo, Config{91, true}) == CS::Ok);
		h.receive(h.client.nextPacket());
		h.server.mailbox.process(0);
		auto packet = h.client.nextPacket();
		CHECK(h.client.onSendAccepted(1, packet.requestId));
		std::array<std::byte, 44> header{};
		const auto reply = resource::protocol::process(h.files.view(), packet.bytes, header);
		CHECK(reply.status == R::Ok && reply.written == 44);
		CHECK(h.client.onReply(header, 1, packet.requestId));
		CHECK(h.client.state() == ClientState::ReadReady && h.client.resultBytes().empty());
		packet = h.client.nextPacket();
		CHECK(pc::detail::load<resource::Cursor>(packet.bytes, 5) ==
		      ::telemetry::protocol::rpc::rpcCursor(1, 32));
		if (zeroProgress) {
			CHECK(h.client.onSendAccepted(1, packet.requestId));
			std::array<std::byte, 12> empty{};
			pc::detail::store(Output{empty}, 1, ::telemetry::protocol::rpc::rpcCursor(1, 32));
			CHECK(h.client.onReply(empty, 1, packet.requestId));
			CHECK(h.client.diagnostic().cause == CS::MalformedReply &&
			      h.client.status() == CS::OutcomeUnknown);
		} else {
			h.receive(packet);
			Config output{};
			CHECK(h.client.decodeResult(output).status == CS::Ok && output.value == 91);
		}
	}
}

static void pointerOutputOverlap() noexcept
{
	alignas(Config*) std::array<std::byte, 128> requestStorage{};
	alignas(Config*) std::array<std::byte, 128> resultStorage{};
	alignas(Config*) std::array<std::byte, 128> packetStorage{};
	Client client{0, requestStorage, resultStorage, packetStorage};
	Harness<> server;
	resource::FileSystem files{std::array{resource::file("/rpc", server.mailbox)}};
	std::array<std::byte, 128> replyStorage{};
	CHECK(client.startSession(1) == CS::Ok);
	CHECK(client.beginService(echo, Config{341, true}) == CS::Ok);
	for (std::uint32_t now = 0; client.active() && now < 20; ++now) {
		client.process(now);
		const auto packet = client.nextPacket();
		if (packet) {
			CHECK(client.onSendAccepted(1, packet.requestId));
			const auto reply =
			    resource::protocol::process(files.view(), packet.bytes, replyStorage);
			CHECK(reply.status == R::Ok);
			CHECK(client.onReply(Input{replyStorage}.first(reply.written), 1, packet.requestId));
		}
		server.mailbox.process(now);
	}
	CHECK(client.completed());
	Config witness{7, false};
	std::array<std::byte, ts::scratchBytes<Config>> scratch{};
	ts::Workspace workspace{scratch};
	for (const auto bytes :
	     {Output{requestStorage}, Output{resultStorage}, Output{packetStorage}}) {
		// Unused tails keep the actual request/header and retained result
		// valid. Placement starts the pointer object's lifetime at alignment.
		auto pointerObject = ::new (bytes.data() + 64) Config*(std::addressof(witness));
		const auto requestBefore = requestStorage;
		const auto resultBefore = resultStorage;
		const auto packetBefore = packetStorage;
		auto lease = workspace.reserve<Config>();
		const auto decoded = client.decodeResult(lease, *pointerObject);
		CHECK(decoded.status == CS::Overlap && decoded.codec == ts::CodecStatus::Overlap);
		CHECK(*pointerObject == std::addressof(witness));
		CHECK(requestStorage == requestBefore && resultStorage == resultBefore &&
		      packetStorage == packetBefore);
		CHECK(!lease.constructed() && client.completed());
		std::destroy_at(pointerObject);
	}
	Config output{};
	CHECK(client.decodeResult(output).status == CS::Ok && output.value == 341 && output.enabled);
}

int main()
{
	operations();
	statuses();
	retriesAndPending();
	deadlinesAndGeneration();
	malformedAndStorage();
	largeRequest();
	readRetryAndRepresentation();
	cursorAndEnvelopeChecks();
	enumArraysAndAgreement();
	headerOnlyReady();
	pointerOutputOverlap();
	std::printf("rpc_client checks=%u failures=%u\n", checks, failures);
	return failures != 0;
}
