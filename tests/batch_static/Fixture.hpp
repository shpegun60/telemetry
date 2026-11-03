/* Actual typed client/server connection, with one bounded FIFO. MIT. */
#ifndef TELEMETRY_TESTS_BATCH_STATIC_FIXTURE_HPP
#define TELEMETRY_TESTS_BATCH_STATIC_FIXTURE_HPP
#pragma once
#include "Contracts.hpp"
#include "Model.hpp"
#include <integration/batch/StaticClient.hpp>
#include <integration/encoded/Backend.hpp>
#include <integration/encoded/BatchDispatcher.hpp>
#include <algorithm>
#include <cstdio>

namespace static_batch_test {
namespace api = static_batch_api;
namespace native = static_batch_fixture;
namespace cl = telemetry::client;
namespace bt = telemetry::integration::batch;
namespace en = telemetry::integration::encoded;
namespace ew = telemetry::protocol::encoded;
namespace bw = ew::batch;
using Data = api::control::types::Config;
using Empty = api::control::types::Empty;
inline unsigned checks = 0;

inline void check(bool ok, unsigned line) noexcept
{
	++checks;
	if (!ok) {
		std::fprintf(stderr, "Static Batch check=%u line=%u failed\n", checks, line);
		std::abort();
	}
}

#define CHECK(...) ::static_batch_test::check(bool(__VA_ARGS__), __LINE__)

inline Data input(std::uint32_t number = 1111) noexcept
{
	return {number,
	        true,
	        {0, UINT16_MAX, 17},
	        static_cast<decltype(Data::mode)>(-123),
	        UINT64_MAX,
	        INT64_MIN,
	        std::bit_cast<float>(0xff812345u),
	        std::bit_cast<double>(UINT64_C(0x8000000000000000))};
}

inline void exact(const Data& value, std::uint32_t number) noexcept
{
	CHECK(value.number_value == number && value.enabled);
	CHECK((value.gains == std::array<std::uint16_t, 3>{0, UINT16_MAX, 17}));
	CHECK(static_cast<std::int16_t>(value.mode) == -123);
	CHECK(value.large == UINT64_MAX && value.negative == INT64_MIN);
	CHECK(std::bit_cast<std::uint32_t>(value.f32) == 0xff812345u);
	CHECK(std::bit_cast<std::uint64_t>(value.f64) == UINT64_C(0x8000000000000000));
}

template<class Codec = api::client::CodecPolicy, std::size_t N = 4, std::size_t Payload = 4096,
         std::size_t Callback = 64>
struct Harness {
	std::uint32_t now = 0;
	cl::Clock clock{this, +[](void* p) noexcept {
		                return static_cast<Harness*>(p)->now;
	                }};
	std::array<std::byte, 4096> tx{}, rx{}, port{}, reply{}, scratch{};
	telemetry::Workspace workspace{scratch};
	en::EncodedBackend backend{tx, rx, clock, 30000, false, true};
	cl::DeviceClient<Codec, N, Payload, Callback> client{backend, clock};
	bw::Capability capability{17, Codec::fingerprint, 32, 4096, 4096};

	Harness() noexcept
	{
		native::reset();
		CHECK(client.admitDescriptor(Codec::descriptorHeader(), 17) == cl::Status::Ok);
	}

	cl::Packet offered() noexcept
	{
		auto packet = client.nextPacket();
		CHECK(packet && packet.mayExecute && packet.bytes.size() <= port.size());
		std::copy(packet.bytes.begin(), packet.bytes.end(), port.begin());
		packet.bytes = cl::Input{port}.first(packet.bytes.size());
		return packet;
	}

	void handoff(cl::Packet packet) noexcept
	{
		CHECK(client.prepareHandoff(packet.session, packet.requestId));
		CHECK(client.onSendAccepted(packet.session, packet.requestId));
	}

	std::size_t response(cl::Packet packet) noexcept
	{
		const auto replyView = en::dispatchEncoded(native::model.view(), workspace, packet.session,
		                                           packet.bytes, reply, capability);
		CHECK(replyView);
		return replyView.written;
	}

	void deliver(cl::Packet packet, std::size_t size) noexcept
	{
		CHECK(client.onReply(cl::Input{reply}.first(size), packet.session, packet.requestId));
		client.process(now);
	}

	void send(cl::Packet packet) noexcept
	{
		handoff(packet);
		deliver(packet, response(packet));
	}

	void stop() noexcept
	{
		client.close();
		for (unsigned i = 0; i < 8 && !client.drained(); ++i)
			client.process(now);
		CHECK(client.drained());
	}
};
} // namespace static_batch_test
#endif
