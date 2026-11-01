/* Canonical byte contract and deterministic malformed-frame probes. MIT. */
#include <wire/ReadManyWire.hpp>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>

namespace ew = telemetry::protocol::encoded;
namespace rm = ew::read_many;
static unsigned checks = 0;

static void require(bool value)
{
	++checks;
	if (!value)
		std::abort();
}

using Input = std::span<const std::byte>;

int main()
{
	const rm::Capability capability{0x12345678u, UINT64_C(0x8877665544332211)};
	std::array<std::byte, 25> bytes{};
	auto out = std::span{bytes}.subspan(1);
	require(bool(rm::writeCapability(out, capability)));
	constexpr std::array<unsigned, 24> golden{0x54, 0x52, 0x4d, 0x31, 1,    32,   1,    1,
	                                          130,  0,    2,    2,    0x78, 0x56, 0x34, 0x12,
	                                          0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
	for (std::size_t i = 0; i < golden.size(); ++i)
		require(std::to_integer<unsigned>(out[i]) == golden[i]);
	require(rm::parseCapability(out).value == capability);
	for (std::size_t size = 0; size < out.size(); ++size) {
		require(!rm::parseCapability(Input{out}.first(size)));
		auto copy = bytes;
		require(!rm::writeCapability(std::span{bytes}.first(size), capability));
		require(bytes == copy);
	}
	for (std::size_t offset : {0u, 4u, 6u, 7u}) {
		auto changed = bytes;
		changed[offset + 1] ^= std::byte{0x80};
		require(!rm::parseCapability(Input{changed}.subspan(1)));
	}
	for (auto invalid : {rm::Capability{}, rm::Capability{1, 0, 0}, rm::Capability{1, 0, 33},
	                     rm::Capability{1, 0, 32, 5}, rm::Capability{1, 0, 32, 131},
	                     rm::Capability{1, 0, 32, 130, 9}, rm::Capability{1, 0, 32, 130, 515}}) {
		const auto before = bytes;
		require(!rm::writeCapability(out, invalid));
		require(before == bytes);
	}

	std::array<std::uint32_t, 32> ids{};
	for (std::size_t i = 0; i < ids.size(); ++i)
		ids[i] = std::uint32_t(i * 65537);
	std::array<std::byte, rm::maxRequestPayload + 1> request{};
	for (std::size_t n = 1; n <= ids.size(); ++n) {
		const auto built = rm::writeRequest(request, std::span{ids}.first(n));
		require(bool(built) && built.written == 2 + n * 4);
		const auto parsed = rm::parseRequest(Input{request}.first(built.written));
		require(bool(parsed) && parsed.count == n);
		for (std::size_t i = 0; i < n; ++i)
			require(parsed.ids[i] == ids[i]);
		for (std::size_t size = 0; size < built.written; ++size)
			require(!rm::parseRequest(Input{request}.first(size)));
		require(!rm::parseRequest(Input{request}.first(built.written + 1)));
	}
	auto duplicateIds = ids;
	duplicateIds[31] = ids[0];
	const auto before = request;
	require(!rm::writeRequest(request, duplicateIds));
	require(request == before);
	require(!rm::writeRequest(request, {}));
	require(!rm::parseRequest({}));
	require(!rm::writeRequest(request, ids, 31));

	// Two ordered records: a full U64 and a missing Field with no payload.
	std::array<std::byte, 27> body{};
	rm::detail::store(body, 0, std::uint16_t{2});
	rm::detail::store(body, 2, ids[0]);
	rm::detail::store(body, 8, std::uint16_t{8});
	for (unsigned i = 0; i < 8; ++i)
		body[10 + i] = std::byte(0xa0u + i);
	rm::detail::store(body, 18, ids[1]);
	body[22] = std::byte{1};
	const auto selected = std::span{ids}.first(2);
	const auto parsed = rm::parseReply(Input{body}.first(26), selected);
	require(bool(parsed) && parsed.count == 2 && parsed.items[0].payload.size() == 8);
	require(parsed.items[1].status == rm::ItemStatus::NotFound && parsed.items[1].payload.empty());
	for (std::size_t size = 0; size < body.size(); ++size)
		if (size != 26)
			require(!rm::parseReply(Input{body}.first(size), selected));
	for (std::size_t offset : {0u, 2u, 6u, 7u, 8u, 18u, 22u, 23u, 24u}) {
		auto changed = body;
		changed[offset] = std::byte{0xff};
		const auto rejected = rm::parseReply(Input{changed}.first(26), selected);
		require(!rejected && rejected.count == 0 && rejected.items[0].payload.empty());
	}

	// No random input may expose a view before the complete body is validated.
	std::uint32_t seed = 0x5a17c0deu;
	std::array<std::byte, 548> frame{};
	for (unsigned iteration = 0; iteration < 4096; ++iteration) {
		for (auto& byte : frame) {
			seed ^= seed << 13;
			seed ^= seed >> 17;
			seed ^= seed << 5;
			byte = std::byte(seed & 255);
		}
		const auto size = iteration % frame.size();
		const auto arbitrary = Input{frame}.first(size);
		(void)ew::parse(arbitrary);
		(void)rm::parseCapability(arbitrary);
		(void)rm::parseRequest(arbitrary);
		const auto reply = rm::parseReply(arbitrary, selected);
		require(!reply && reply.count == 0 && reply.items[0].payload.empty());
	}
	ew::Header header{false, ew::Operation::ReadMany, 0, 1, 1, 0, 6, 514, 0};
	require(ew::validate(header) == ew::Status::Ok);
	header.reply = true;
	header.payloadBytes = 10;
	require(ew::validate(header) == ew::Status::Ok);
	header.endpointStatus = 1;
	require(ew::validate(header) == ew::Status::InvalidHeader);
	header.endpointStatus = 0;
	header.operation = ew::Operation::FieldRead;
	require(ew::validate(header) == ew::Status::InvalidHeader); // Legacy exact size.
	std::printf("ReadMany wire: %u checks, 0 failures\n", checks);
}
