/*
 * @file main.cpp
 * @brief Run telemetry and resource requests through the real COBS endpoint.
 *
 * The device facade owns application objects and telemetry bindings. COBS
 * owns framed RX/TX blocks. A small fake byte transport demonstrates splitting,
 * borrowed TX lifetime and Busy handling without Qt, a UART driver or a board.
 * Payloads keep the canonical telemetry/resource byte format unchanged.
 * Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
 */
#include "../device_integration/Api.hpp"
#include <telemetry/Telemetry.hpp>
#include <cobs/Cobs.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <span>
#include <source_location>

namespace {
using Link =
    cobs::Endpoint<wire::Pool<8, 2>, cobs::Format<crc::Crc16Bitwise, app::api::MaxBodyBytes>>;
using Input = std::span<const std::uint8_t>;
unsigned checks = 0;

void require(bool condition, std::source_location location = std::source_location::current())
{
	++checks;
	if (!condition) {
		std::fprintf(stderr, "COBS integration check %u failed at line %lu\n", checks,
		             static_cast<unsigned long>(location.line()));
		std::abort();
	}
}

// An accepted send borrows the exact encoded frame until finish(). The two
// transports outlive the endpoints bound to them, just as a UART driver must.
// Public methods: send() borrow frame; busy() query ownership;
// frame() view bytes; finish() release frame.
class Transport {
public:
	bool send(Input frame) noexcept
	{
		if (busy_)
			return false;
		borrowed_ = frame;
		busy_ = true;
		return true;
	}

	bool busy() const noexcept
	{
		return busy_;
	}

	Input frame() const noexcept
	{
		return borrowed_;
	}

	void finish() noexcept
	{
		busy_ = false;
		borrowed_ = {};
	}

private:
	Input borrowed_{};
	bool busy_ = false;
};

void bind(Link& endpoint, Transport& transport)
{
	require(endpoint.bind(Link::Sender{tiny::bind<&Transport::send>(transport)},
	                      Link::BusyQuery{tiny::bind<&Transport::busy>(transport)}));
}

// Feed arbitrary byte chunks, then report completion to the sending endpoint.
// consume() assembles COBS frames; a chunk is never treated as one packet.
void transfer(Link& sender, Transport& transport, Link& receiver)
{
	const auto frame = transport.frame();
	require(transport.busy() && sender.tx_active() && !frame.empty());
	require(frame.back() == 0);
	for (std::size_t offset = 0; offset < frame.size();) {
		const auto count = std::min(std::size_t{3}, frame.size() - offset);
		receiver.consume(frame.subspan(offset, count));
		offset += count;
	}
	transport.finish();
	sender.poll(0);
	require(!sender.tx_active());
}

// No length-prefix StreamReceiver is used here: COBS already supplies complete
// payload packets. The application receives only route/operation/ID/data.
Link::Packet roundTrip(Link& client, Transport& clientTransport, Link& device,
                       Transport& deviceTransport, Link::Message& request)
{
	require(client.send(request) == wire::SendResult::Sent);
	require(!request);
	transfer(client, clientTransport, device);
	auto incoming = device.pop_packet();
	require(static_cast<bool>(incoming));

	std::array<std::byte, app::api::MaxBodyBytes> output{};
	const auto result = app::api::onCompletePacket(std::as_bytes(incoming.data()), output);
	require(result.status == app::api::PacketStatus::Replied && result.written != 0);
	auto response = device.make_message(result.written);
	require(static_cast<bool>(response));
	// uint8_t is the unsigned-char byte view accepted by the COBS builder.
	require(response.append_bytes(
	    Input{reinterpret_cast<const std::uint8_t*>(output.data()), result.written}));
	incoming.reset();
	require(device.send(response) == wire::SendResult::Sent);
	transfer(device, deviceTransport, client);
	auto reply = client.pop_packet();
	require(static_cast<bool>(reply));
	return reply;
}

Link::Message direct(Link& client, app::api::Operation operation, std::uint32_t id)
{
	auto request = client.make_message();
	require(static_cast<bool>(request));
	require(request.append_le(static_cast<std::uint8_t>(app::api::Route::Direct)));
	require(request.append_le(static_cast<std::uint8_t>(operation)));
	require(request.append_le(id));
	return request;
}

template<class T>
void appendValue(Link::Message& message, const T& value)
{
	std::array<std::byte, telemetry::wireSize<T>> bytes{};
	require(telemetry::encode(value, bytes) == telemetry::CodecStatus::Ok);
	require(message.append_bytes(
	    Input{reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()}));
}

void directStatus(const Link::Packet& reply, std::uint8_t endpoint)
{
	require(reply.size() >= 3 && reply.data()[0] == 1);
	require(reply.data()[1] == 0 && reply.data()[2] == endpoint);
}

// Bounds-checked wire readers parse only the application header. Native DTOs
// still use telemetry's structural codec: append_native(struct) is never used.
std::uint32_t scalar32(Input bytes, std::size_t offset)
{
	std::uint32_t value = 0;
	require(wire::read_le(bytes, offset, value));
	return value;
}

void checkTelemetry(Link& client, Transport& ct, Link& device, Transport& dt)
{
	auto request = direct(client, app::api::Operation::Read, 0);
	auto reply = roundTrip(client, ct, device, dt, request);
	directStatus(reply, 0);
	require(reply.size() == 7 && scalar32(reply.data(), 3) == 10);
	reply.reset();

	request = direct(client, app::api::Operation::Write, 0);
	appendValue(request, std::uint32_t{25});
	reply = roundTrip(client, ct, device, dt, request);
	directStatus(reply, 0); // WriteResult::Applied.
	require(reply.size() == 3);
	reply.reset();
	request = direct(client, app::api::Operation::Read, 0);
	reply = roundTrip(client, ct, device, dt, request);
	require(scalar32(reply.data(), 3) == 25);
	reply.reset();

	request = direct(client, app::api::Operation::Write, 0);
	appendValue(request, std::uint32_t{0}); // Business rule, not wire failure.
	reply = roundTrip(client, ct, device, dt, request);
	directStatus(reply, 3); // WriteResult::InvalidValue.
	reply.reset();

	request = direct(client, app::api::Operation::Command, 0);
	reply = roundTrip(client, ct, device, dt, request);
	directStatus(reply, 0); // CommandResult::Executed.
	reply.reset();
	request = direct(client, app::api::Operation::Service, 1);
	appendValue(request, app::Query{0});
	reply = roundTrip(client, ct, device, dt, request);
	directStatus(reply, 0); // ServiceStatus::Ok.
	require(reply.size() == 3 + telemetry::wireSize<app::Snapshot>);
	alignas(app::Snapshot) std::array<std::byte, sizeof(app::Snapshot)> scratch{};
	telemetry::Workspace workspace{scratch};
	auto lease = workspace.reserve<app::Snapshot>();
	app::Snapshot* snapshot = nullptr;
	require(telemetry::decode<app::Snapshot>(std::as_bytes(reply.data().subspan(3)), lease,
	                                         snapshot) == telemetry::CodecStatus::Ok);
	require(snapshot->gain == 100 && snapshot->displayEnabled);
	reply.reset();

	// A malformed bool is rejected before a setter reaches application code.
	const auto before = app::api::diagnostics().device.writes;
	request = direct(client, app::api::Operation::Write, 3);
	require(request.append_le(std::uint8_t{50}));
	require(request.append_le(std::uint8_t{2}));
	reply = roundTrip(client, ct, device, dt, request);
	require(reply.size() == 3 && reply.data()[1] == 5);
	require(app::api::diagnostics().device.writes == before);
}

Link::Message resourceRequest(Link& client, std::uint8_t operation)
{
	auto request = client.make_message();
	require(static_cast<bool>(request));
	require(request.append_le(std::uint8_t{2})); // Application Resource route.
	require(request.append_le(operation));
	return request;
}

void checkResources(Link& client, Transport& ct, Link& device, Transport& dt)
{
	auto request = resourceRequest(client, 1); // LIST.
	require(request.append_le(std::uint64_t{0}));
	auto reply = roundTrip(client, ct, device, dt, request);
	require(reply.size() > 13 && reply.data()[0] == 2 && reply.data()[1] == 0);
	require(reply.data()[10] == 1); // LIST eof, after the application route.
	std::size_t offset = 11;
	std::uint16_t dataSize = 0;
	require(wire::read_le(reply.data(), offset, dataSize));
	require(reply.size() == offset + dataSize);
	std::uint16_t pathSize = 0;
	require(wire::read_le(reply.data(), offset, pathSize));
	Input path;
	require(wire::read_bytes(reply.data(), offset, pathSize, path));
	const char expected[] = "/device/version.bin";
	require(path.size() == sizeof(expected) - 1 && std::equal(path.begin(), path.end(), expected));
	reply.reset();

	request = resourceRequest(client, 2); // STAT index 0.
	require(request.append_le(std::uint32_t{0}));
	reply = roundTrip(client, ct, device, dt, request);
	require(reply.size() == 7 && reply.data()[1] == 0);
	require(scalar32(reply.data(), 2) == 4 && reply.data()[6] == 1);
	reply.reset();

	request = resourceRequest(client, 3); // READ index 0, initial cursor.
	require(request.append_le(std::uint32_t{0}));
	require(request.append_le(std::uint64_t{0}));
	reply = roundTrip(client, ct, device, dt, request);
	require(reply.size() == 17 && reply.data()[1] == 0 && reply.data()[10] == 1);
	require(scalar32(reply.data(), 13) == 1);
	reply.reset();

	request = resourceRequest(client, 4); // WRITE a three-byte final chunk.
	require(request.append_le(std::uint32_t{1}));
	require(request.append_le(std::uint64_t{0}));
	require(request.append_le(std::uint8_t{1}));
	require(request.append_le(std::uint16_t{3}));
	const std::array<std::uint8_t, 3> note{0, 1, 2};
	require(request.append_bytes(note));
	reply = roundTrip(client, ct, device, dt, request);
	require(reply.size() == 15 && reply.data()[1] == 0);
	require(scalar32(reply.data(), 10) == 2 && reply.data()[14] == 0);
	reply.reset();

	request = resourceRequest(client, 4); // Send only the unconsumed suffix.
	require(request.append_le(std::uint32_t{1}));
	require(request.append_le(std::uint64_t{2}));
	require(request.append_le(std::uint8_t{1}));
	require(request.append_le(std::uint16_t{1}));
	require(request.append_bytes(Input{note}.subspan(2)));
	reply = roundTrip(client, ct, device, dt, request);
	require(scalar32(reply.data(), 10) == 1 && reply.data()[14] == 1);
}

// Locate a nonzero decoded payload byte without changing COBS code bytes,
// length, delimiter or CRC. The receiver must reject integrity, not framing.
bool corruptPayload(std::span<std::uint8_t> frame, std::size_t target)
{
	std::size_t encoded = 0, decoded = 0;
	while (encoded < frame.size() && frame[encoded] != 0) {
		const auto code = frame[encoded++];
		if (code - 1u > frame.size() - encoded)
			return false;
		for (unsigned i = 1; i < code; ++i, ++encoded, ++decoded) {
			if (decoded == target) {
				// Keep the encoded data byte nonzero so block geometry is intact.
				if (frame[encoded] == 0 || frame[encoded] == 1)
					return false;
				frame[encoded] ^= 1;
				return true;
			}
		}
		if (code != 0xff && encoded < frame.size() && frame[encoded] != 0)
			++decoded; // Zero represented by the boundary between COBS blocks.
	}
	return false;
}

void checkIntegrity(Link& client, Transport& ct, Link& device, Transport& dt)
{
	const auto before = device.stats().rx;
	const auto rxAvailable = device.storage().rx_available();
	const auto txAvailable = client.storage().tx_available();
	const auto callbacks = app::api::diagnostics();
	auto request = direct(client, app::api::Operation::Write, 0);
	appendValue(request, std::uint32_t{42});
	require(client.send(request) == wire::SendResult::Sent && !request);
	const auto original = ct.frame();
	std::array<std::uint8_t,
	           cobs::codec::max_wire_size(Link::length_size + Link::max_send_size + Link::crc_size)>
	    storage{};
	require(original.size() <= storage.size() && original.back() == 0);
	// The explicit bound also keeps GCC's ARM array-bounds analysis local.
	std::copy_n(original.data(), std::min(original.size(), storage.size()), storage.data());
	auto damaged = std::span{storage}.first(original.size());
	// First value byte follows the length and six-byte Direct application header.
	require(corruptPayload(damaged, Link::length_size + 6));
	unsigned changed = 0;
	for (std::size_t i = 0; i < damaged.size(); ++i)
		changed += damaged[i] != original[i];
	require(changed == 1 && damaged.back() == 0);
	for (std::size_t offset = 0; offset < damaged.size();) {
		const auto count = std::min(std::size_t{3}, damaged.size() - offset);
		device.consume(damaged.subspan(offset, count));
		offset += count;
	}
	ct.finish();
	client.poll(0);
	require(!client.tx_active() && client.storage().tx_available() == txAvailable);
	require(!device.pop_packet()); // No application body can reach packet routing.
	const auto after = device.stats().rx;
	require(after.crc_errors == before.crc_errors + 1);
	require(after.frames_lost == before.frames_lost + 1);
	require(after.frames_received == before.frames_received);
	require(after.malformed == before.malformed &&
	        after.length_mismatch == before.length_mismatch && after.oversize == before.oversize &&
	        after.resyncs == before.resyncs);
	require(device.storage().rx_available() == rxAvailable);
	const auto unchanged = app::api::diagnostics();
	require(unchanged.device.reads == callbacks.device.reads &&
	        unchanged.device.writes == callbacks.device.writes &&
	        unchanged.device.commands == callbacks.device.commands &&
	        unchanged.device.services == callbacks.device.services &&
	        unchanged.fileReads == callbacks.fileReads &&
	        unchanged.fileWrites == callbacks.fileWrites);
	// The failed CRC consumed its delimiter; the next valid request works directly.
	request = direct(client, app::api::Operation::Read, 0);
	auto reply = roundTrip(client, ct, device, dt, request);
	directStatus(reply, 0);
	require(reply.size() == 7 && scalar32(reply.data(), 3) == 10);
	require(device.stats().rx.frames_received == before.frames_received + 1);
	require(device.stats().rx.crc_errors == before.crc_errors + 1);
}

void checkCapacity(Link& client, Transport& ct, Link& device, Transport& dt)
{
	static_assert(Link::max_send_size == 128 && Link::max_receive_size == 128);
	const auto clientTx = client.storage().tx_available();
	const auto clientRx = client.storage().rx_available();
	const auto deviceTx = device.storage().tx_available();
	const auto deviceRx = device.storage().rx_available();
	const auto clientBefore = client.stats().rx;
	const auto deviceBefore = device.stats().rx;
	std::array<std::uint8_t, Link::max_send_size> payload{};
	for (std::size_t i = 0; i < payload.size(); ++i)
		payload[i] = i % 7 == 0 ? 0 : static_cast<std::uint8_t>(i + 1);
	// This echo tests COBS capacity independently of any particular DTO or route.
	// All 128 application bytes are delivered in each direction, including zeros.
	auto tooLarge = client.make_message(payload.size() + 1);
	require(!tooLarge && client.storage().tx_available() == clientTx);
	auto request = client.make_message(payload.size());
	require(request && request.size() == 0 && request.capacity() == payload.size());
	require(request.append_bytes(payload) && request.size() == payload.size());
	require(!request.append_le(std::uint8_t{1}) && request.size() == payload.size());
	require(client.send(request) == wire::SendResult::Sent && !request);
	require(client.storage().tx_available() == clientTx - 1 && client.tx_active());
	const auto borrowed = ct.frame();
	require(!borrowed.empty() && borrowed.back() == 0);
	auto pending = client.make_message(payload.size());
	require(pending && pending.append_bytes(payload));
	require(client.send(pending) == wire::SendResult::Busy && pending.size() == payload.size());
	require(ct.frame().data() == borrowed.data() && ct.frame().size() == borrowed.size());
	transfer(client, ct, device);
	require(client.storage().tx_available() == clientTx - 1); // pending still owns its TX block.
	auto incoming = device.pop_packet();
	require(incoming && incoming.size() == payload.size());
	require(std::equal(incoming.data().begin(), incoming.data().end(), payload.begin()));
	require(device.storage().rx_available() == deviceRx - 1);
	auto response = device.make_message(payload.size());
	require(response && response.append_bytes(incoming.data()));
	require(device.send(response) == wire::SendResult::Sent && !response);
	require(device.storage().tx_available() == deviceTx - 1);
	incoming.reset(); // Reply has its own TX block; the decoded RX borrow is done.
	require(device.storage().rx_available() == deviceRx);
	transfer(device, dt, client);
	require(device.storage().tx_available() == deviceTx);
	auto reply = client.pop_packet();
	require(reply && reply.size() == payload.size());
	require(std::equal(reply.data().begin(), reply.data().end(), payload.begin()));
	require(client.storage().rx_available() == clientRx - 1);
	reply.reset();
	pending = {};
	require(client.storage().rx_available() == clientRx &&
	        client.storage().tx_available() == clientTx);
	require(client.stats().rx.frames_received == clientBefore.frames_received + 1 &&
	        client.stats().rx.frames_lost == clientBefore.frames_lost);
	require(device.stats().rx.frames_received == deviceBefore.frames_received + 1 &&
	        device.stats().rx.frames_lost == deviceBefore.frames_lost);
}

void checkOwnership(Link& client, Transport& ct, Link& device)
{
	auto first = client.make_message();
	auto pending = client.make_message();
	require(first.append_le(std::uint8_t{1}) && pending.append_le(std::uint8_t{2}));
	require(client.send(first) == wire::SendResult::Sent);
	require(client.send(pending) == wire::SendResult::Busy && static_cast<bool>(pending));
	transfer(client, ct, device);
	auto packet = device.pop_packet();
	require(packet && packet.data()[0] == 1);
	packet.reset();
	require(client.send(pending) == wire::SendResult::Sent && !pending);
	transfer(client, ct, device);
	packet = device.pop_packet();
	require(packet && packet.data()[0] == 2);
	packet.reset();

	// Physical byte loss abandons the in-flight frame. Already decoded packets
	// remain queued; the integration never turns each short chunk into a gap.
	auto request = client.make_message();
	require(request.append_le(std::uint8_t{3}));
	require(client.send(request) == wire::SendResult::Sent);
	const auto frame = ct.frame();
	device.consume(frame.first(2));
	device.notify_gap();
	// The receiver discards bytes through the next delimiter after a gap.
	// Supply the abandoned frame's delimiter before the next intact frame.
	device.consume(frame.last(1));
	ct.finish();
	client.poll(0);
	require(!device.pop_packet());
	request = client.make_message();
	require(request.append_le(std::uint8_t{4}));
	require(client.send(request) == wire::SendResult::Sent);
	transfer(client, ct, device);
	packet = device.pop_packet();
	require(packet && packet.data()[0] == 4);
}
} // namespace

int main()
{
	Transport clientTransport, deviceTransport;
	Link client, device;
	bind(client, clientTransport);
	bind(device, deviceTransport);
	checkTelemetry(client, clientTransport, device, deviceTransport);
	checkResources(client, clientTransport, device, deviceTransport);
	checkOwnership(client, clientTransport, device);
	checkIntegrity(client, clientTransport, device, deviceTransport);
	checkCapacity(client, clientTransport, device, deviceTransport);
	require(client.unbind() && device.unbind());
	std::printf("COBS telemetry/resource integration: %u checks passed\n", checks);
}
