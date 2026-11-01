/* Deterministic ordinary Resource bootstrap controls; no port or hardware. MIT. */
#include "DescriptorBootstrap.hpp"
#include "SerialTransport.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <string_view>
#include <vector>

namespace {
namespace rc = resource::protocol::client;
namespace many = telemetry::protocol::encoded::read_many;
using Bootstrap = inspector_qml::DescriptorBootstrap;
using Bytes = std::vector<std::byte>;
using Input = std::span<const std::byte>;
using Op = resource::protocol::Op;
unsigned checks = 0;

#define CHECK(expression)                                                                          \
	do {                                                                                           \
		++checks;                                                                                  \
		if (!(expression)) {                                                                       \
			std::fprintf(stderr, "ReadMany bootstrap check %u line %d: %s\n", checks, __LINE__,    \
			             #expression);                                                             \
			std::abort();                                                                          \
		}                                                                                          \
	} while (false)

template<class T>
void store(Bytes& bytes, std::size_t offset, T value)
{
	for (std::size_t i = 0; i < sizeof(T); ++i)
		bytes[offset + i] = std::byte((std::uint64_t(value) >> (8 * i)) & 0xffu);
}

Bytes chunk(resource::Cursor next, bool eof, Input payload)
{
	Bytes bytes(resource::protocol::wire::chunkReplyHeaderSize + payload.size());
	bytes[0] = std::byte(resource::Status::Ok);
	store(bytes, 1, next);
	bytes[9] = std::byte(eof);
	store(bytes, 10, static_cast<std::uint16_t>(payload.size()));
	std::copy(payload.begin(), payload.end(), bytes.begin() + 12);
	return bytes;
}

Bytes list(resource::Cursor next, bool eof, std::initializer_list<std::string_view> paths)
{
	Bytes payload;
	for (const auto path : paths) {
		const auto offset = payload.size();
		payload.resize(offset + 2 + path.size());
		store(payload, offset, static_cast<std::uint16_t>(path.size()));
		for (std::size_t i = 0; i < path.size(); ++i)
			payload[offset + 2 + i] = std::byte(static_cast<unsigned char>(path[i]));
	}
	return chunk(next, eof, payload);
}

Bytes stat(resource::FileSize size, resource::FileFlags flags = resource::FileFlag::Readable)
{
	Bytes bytes(resource::protocol::wire::statReplySize);
	bytes[0] = std::byte(resource::Status::Ok);
	store(bytes, 1, size);
	bytes[5] = std::byte(flags);
	return bytes;
}

void request(const Bootstrap& bootstrap, Op operation, resource::FileIndex index = 0,
             resource::Cursor cursor = 0)
{
	std::array<std::byte, 16> actual{}, expected{};
	const auto built = bootstrap.request(actual);
	const auto wanted = operation == Op::List   ? rc::makeList(expected, cursor)
	                    : operation == Op::Stat ? rc::makeStat(expected, index)
	                                            : rc::makeRead(expected, index, cursor);
	CHECK(bootstrap.operation() == operation);
	CHECK(built.status == rc::BuildStatus::Ok && wanted.status == rc::BuildStatus::Ok);
	CHECK(built.written == wanted.written &&
	      std::equal(actual.begin(), actual.begin() + built.written, expected.begin()));
}

std::array<std::byte, many::capabilityBytes> capability()
{
	std::array<std::byte, many::capabilityBytes> bytes{};
	CHECK(bool(many::writeCapability(bytes, {7, 0x0123456789abcdefULL})));
	return bytes;
}

Bootstrap listed()
{
	Bootstrap bootstrap;
	bootstrap.reset(true);
	CHECK(bootstrap.accept(
	    list(3, true,
	         {"/device/version.bin", "/telemetry/descriptor.bin", "/telemetry/read-many.bin"})));
	CHECK(!bootstrap.complete());
	request(bootstrap, Op::Stat, 1);
	return bootstrap;
}

Bootstrap capabilityStat()
{
	auto bootstrap = listed();
	CHECK(bootstrap.accept(stat(64)));
	request(bootstrap, Op::Read, 1);
	std::array<std::byte, 64> descriptor{};
	descriptor.fill(std::byte{0xa5});
	CHECK(bootstrap.accept(chunk(64, true, descriptor)));
	CHECK(!bootstrap.complete() && bootstrap.descriptor().size() == 64 &&
	      bootstrap.capability().isEmpty());
	request(bootstrap, Op::Stat, 2);
	return bootstrap;
}

Bootstrap capabilityRead()
{
	auto bootstrap = capabilityStat();
	CHECK(bootstrap.accept(stat(many::capabilityBytes)));
	request(bootstrap, Op::Read, 2);
	return bootstrap;
}

void positiveAndLegacy()
{
	Bootstrap bootstrap;
	bootstrap.reset(true);
	request(bootstrap, Op::List);
	CHECK(bootstrap.accept(list(1, false, {"/device/version.bin"})));
	request(bootstrap, Op::List, 0, 1);
	CHECK(bootstrap.accept(list(2, false, {"/telemetry/read-many.bin"})));
	request(bootstrap, Op::List, 0, 2);
	CHECK(bootstrap.accept(list(3, true, {"/telemetry/descriptor.bin"})));
	request(bootstrap, Op::Stat, 2);
	CHECK(bootstrap.accept(stat(64)));
	std::array<std::byte, 64> descriptor{};
	for (std::size_t i = 0; i < descriptor.size(); ++i)
		descriptor[i] = std::byte(i);
	CHECK(bootstrap.accept(chunk(17, false, Input{descriptor}.first(17))));
	request(bootstrap, Op::Read, 2, 17);
	CHECK(bootstrap.accept(chunk(64, true, Input{descriptor}.subspan(17))));
	request(bootstrap, Op::Stat, 1);
	CHECK(!bootstrap.complete() && bootstrap.capability().isEmpty());
	CHECK(bootstrap.accept(stat(many::capabilityBytes)));
	const auto cap = capability();
	CHECK(bootstrap.accept(chunk(7, false, Input{cap}.first(7))));
	request(bootstrap, Op::Read, 1, 7);
	auto tail = chunk(24, true, Input{cap}.subspan(7));
	CHECK(bootstrap.accept(tail));
	CHECK(bootstrap.complete());
	CHECK(bootstrap.descriptor() ==
	      QByteArray(reinterpret_cast<const char*>(descriptor.data()), 64));
	CHECK(bootstrap.capability() == QByteArray(reinterpret_cast<const char*>(cap.data()), 24));
	std::fill(tail.begin(), tail.end(), std::byte{});
	CHECK(bootstrap.capability() == QByteArray(reinterpret_cast<const char*>(cap.data()), 24));
	CHECK(!bootstrap.accept(chunk(24, true, cap)));
	std::array<std::byte, 16> output{};
	CHECK(bootstrap.request(output).status != rc::BuildStatus::Ok);
	const auto owned = bootstrap.capability();
	bootstrap.reset();
	CHECK(!bootstrap.complete() && bootstrap.descriptor().isEmpty() &&
	      bootstrap.capability().isEmpty());
	CHECK(owned.size() == 24);
	// Legacy mode still requires only Descriptor and ignores unrelated TRM1 files.
	CHECK(bootstrap.accept(list(
	    3, true,
	    {"/telemetry/read-many.bin", "/telemetry/descriptor.bin", "/telemetry/read-many.bin"})));
	CHECK(bootstrap.accept(stat(64)));
	CHECK(bootstrap.accept(chunk(64, true, descriptor)));
	CHECK(bootstrap.complete() && bootstrap.capability().isEmpty());
	inspector_qml::SerialTransport transport;
	transport.setReadManyDiscovery(true);
	CHECK(!transport.ready() && transport.readManyCapability().isEmpty());
	transport.close();
	CHECK(transport.locallyQuiescent() && transport.readManyCapability().isEmpty());
}

void listFailures()
{
	for (const auto& paths :
	     {list(1, true, {"/telemetry/descriptor.bin"}), list(1, true, {"/telemetry/read-many.bin"}),
	      list(3, true,
	           {"/telemetry/descriptor.bin", "/telemetry/descriptor.bin",
	            "/telemetry/read-many.bin"}),
	      list(3, true,
	           {"/telemetry/descriptor.bin", "/telemetry/read-many.bin",
	            "/telemetry/read-many.bin"}),
	      list(0, false, {}), list(2, true, {"/telemetry/descriptor.bin"})}) {
		Bootstrap bootstrap;
		bootstrap.reset(true);
		CHECK(!bootstrap.accept(paths) && !bootstrap.complete());
	}
	Bootstrap duplicate;
	duplicate.reset(true);
	CHECK(duplicate.accept(
	    list(2, false, {"/telemetry/descriptor.bin", "/telemetry/read-many.bin"})));
	CHECK(!duplicate.accept(list(3, true, {"/telemetry/read-many.bin"})));
	Bootstrap empty;
	empty.reset(true);
	CHECK(!empty.accept({}));
	const std::array shortError{std::byte(resource::Status::InvalidData)};
	CHECK(!empty.accept(shortError));
}

void fileFailures()
{
	for (const auto size : {0u, 63u, telemetry::Limits::maxDescriptorBytes + 1u}) {
		auto bootstrap = listed();
		CHECK(!bootstrap.accept(stat(size)) && !bootstrap.complete());
	}
	for (const auto size : {0u, 23u, 25u}) {
		auto bootstrap = capabilityStat();
		CHECK(!bootstrap.accept(stat(size)) && !bootstrap.complete());
	}
	{
		auto bootstrap = capabilityStat();
		CHECK(!bootstrap.accept(stat(24, resource::FileFlag::None)));
	}
	const auto cap = capability();
	for (const auto& reply :
	     {chunk(0, false, {}), chunk(23, true, Input{cap}.first(23)), chunk(24, false, cap),
	      chunk(25, true, cap), chunk(24, true, Input{cap}.first(23))}) {
		auto bootstrap = capabilityRead();
		CHECK(!bootstrap.accept(reply) && !bootstrap.complete());
	}
	{
		auto bootstrap = capabilityRead();
		auto reply = chunk(24, true, cap);
		reply.pop_back();
		CHECK(!bootstrap.accept(reply));
	}
	{
		auto bootstrap = capabilityRead();
		CHECK(bootstrap.accept(chunk(7, false, Input{cap}.first(7))));
		CHECK(!bootstrap.accept(chunk(24, true, cap)));
	}
}

void capabilityFailures()
{
	const auto original = capability();
	for (const auto offset : {0u, 4u, 6u, 7u}) {
		auto cap = original;
		cap[offset] ^= std::byte{0x40};
		auto bootstrap = capabilityRead();
		CHECK(!bootstrap.accept(chunk(24, true, cap)) && !bootstrap.complete());
	}
	for (const auto offset : {5u, 8u, 10u, 12u}) {
		auto cap = original;
		const auto count = offset == 5 ? 1u : offset == 12 ? 4u : 2u;
		std::fill_n(cap.begin() + offset, count, std::byte{});
		auto bootstrap = capabilityRead();
		CHECK(!bootstrap.accept(chunk(24, true, cap)) && !bootstrap.complete());
	}
	{
		auto cap = original;
		cap[5] = std::byte{33};
		auto bootstrap = capabilityRead();
		CHECK(!bootstrap.accept(chunk(24, true, cap)));
	}
	// This layer checks syntax only: matching the actual parsed Descriptor is
	// the CLI's admission responsibility, rather than a second schema parser.
	{
		auto cap = original;
		cap[16] ^= std::byte{1};
		auto bootstrap = capabilityRead();
		CHECK(bootstrap.accept(chunk(24, true, cap)) && bootstrap.complete());
	}
}
} // namespace

int main()
{
	positiveAndLegacy();
	listFailures();
	fileFailures();
	capabilityFailures();
	std::printf("ReadMany bootstrap checks=%u passed, hardware=0\n", checks);
}
