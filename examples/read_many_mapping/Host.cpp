/*
 * @file Host.cpp
 * @brief Offline comparison and negative controls for two experimental ReadMany mappings.
 * @author Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT
 */
#include "Fixture.hpp"
#include "../read_comparison/Framing.hpp"
#include <integration/encoded/Dispatcher.hpp>
#include <resource/BytesFile.hpp>
#include <resource/FileSystem.hpp>
#include <resource/protocol/Packets.hpp>
#include <resource/protocol/Protocol.hpp>
#include <wire/EncodedWire.hpp>
#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <span>
#include <string_view>

namespace rm = read_many_mapping;
namespace fx = rm::fixture;
namespace ts = telemetry;
namespace ew = telemetry::protocol::encoded;
namespace en = telemetry::integration::encoded;
namespace rp = resource::protocol;
namespace packets = resource::protocol::client;
using Steady = std::chrono::steady_clock;

namespace {
std::uint64_t checks = 0;

void check(bool condition, unsigned line)
{
	++checks;
	if (!condition) {
		std::fprintf(stderr, "ReadMany mapping check=%" PRIu64 " failed at line %u\n", checks,
		             line);
		std::exit(1);
	}
}

#define CHECK(condition) check(bool(condition), __LINE__)
constexpr std::size_t scratchCapacity = fx::serviceModel.maxScratch() + 32;

struct Context {
	fx::Metadata metadata;
	rm::Capability capability;
	ts::ModelView model;
	std::array<std::byte, 2048> request{}, reply{};
	std::array<std::byte, scratchCapacity> scratch{};
	ts::Workspace workspace{scratch};
	rm::Batch batch{};

	Context(rm::Input descriptor = fx::scalarDescriptorBytes,
	        ts::ModelView source = fx::scalarModel.view()) noexcept
	    : metadata(descriptor), model(source)
	{
		CHECK(metadata.valid);
		capability.fingerprint = metadata.schema.fingerprint();
	}

	~Context()
	{
		fx::serviceOwner.workspace = nullptr;
		fx::serviceOwner.batch = nullptr;
	}
};

struct Cycle {
	std::array<std::uint32_t, rm::maxItems> values{};
	std::uint64_t getterCalls = 0, protocolTx = 0, protocolRx = 0, framedTx = 0, framedRx = 0;
	std::uint64_t servicePackBytes = 0, serviceIdCopyBytes = 0;
};

struct Mode {
	const char* name = nullptr;
	Cycle cycle{};
	std::uint64_t elapsed = 0, dispatcher = 0, checksum = 0, workChecksum = 0;
};

struct Profile {
	std::size_t count = 0;
	std::uint32_t getterWork = 0;
	std::array<std::uint32_t, rm::maxItems> ids{};
	std::array<Mode, 2> modes{};
};

struct Control {
	const char* name;
	std::uint64_t checks = 0;
};

std::array controls{Control{"capability"},       Control{"request_preflight"},
                    Control{"reply_validation"}, Control{"schema_session"},
                    Control{"mixed_values"},     Control{"service_errors"}};

std::uint64_t elapsed(Steady::time_point start) noexcept
{
	return std::uint64_t(
	    std::chrono::duration_cast<std::chrono::nanoseconds>(Steady::now() - start).count());
}

std::uint64_t checksum(std::span<const std::uint32_t> values) noexcept
{
	std::uint64_t sum = 0;
	for (auto value : values)
		sum = sum * 33 + value;
	return sum;
}

void frames(Cycle& result, rm::Input request, rm::Input reply, bool account)
{
	result.protocolTx = request.size();
	result.protocolRx = reply.size();
	if (account) {
		result.framedTx = read_comparison::framedBytes(request);
		result.framedRx = read_comparison::framedBytes(reply);
		CHECK(result.framedTx && result.framedRx);
	}
}

Cycle compact(Context& context, const rm::CapturedRequest& captured, bool account, bool timed,
              std::uint64_t& dispatcher)
{
	Cycle result;
	const auto before = fx::scalarOwner.total();
	const auto built = rm::writeRequest(context.request, captured, context.capability);
	CHECK(built);
	const auto request = rm::Input{context.request}.first(built.written);
	const auto start = timed ? Steady::now() : Steady::time_point{};
	const auto processed = rm::dispatch(context.model, context.workspace, context.capability,
	                                    request, context.reply, context.batch);
	if (timed)
		dispatcher += elapsed(start);
	CHECK(processed);
	const auto reply = rm::Input{context.reply}.first(processed.written);
	const auto parsed = rm::validateReply(reply, captured, context.capability);
	CHECK(parsed && parsed.value.operation == rm::GlobalStatus::Ok &&
	      parsed.value.count == captured.count());
	for (std::size_t i = 0; i < captured.count(); ++i) {
		const auto& item = parsed.value.items[i];
		CHECK(item.status == rm::ItemStatus::Ok && item.payload.size() == 4);
		const auto view = context.metadata.schema.view(item.typeId, item.payload);
		CHECK(view);
		const auto number = view.value.get<std::uint32_t>();
		CHECK(number);
		result.values[i] = number.value;
	}
	result.getterCalls = fx::scalarOwner.total() - before;
	frames(result, request, reply, account);
	if (account) {
		context.request.fill(std::byte{0xa5});
		context.reply.fill(std::byte{0x5a});
		context.batch = {};
	}
	return result;
}

rm::FrameResult serviceRequest(Context& context, const fx::FixedRequest& request)
{
	const auto payload =
	    rm::Output{context.request}.subspan(ew::headerBytes, ts::wireSize<fx::FixedRequest>);
	CHECK(ts::encode(request, payload) == ts::CodecStatus::Ok);
	ew::Header header;
	header.operation = ew::Operation::Service;
	header.session = context.capability.session;
	header.requestId = 1;
	header.payloadBytes = ts::wireSize<fx::FixedRequest>;
	header.expectedResponseBytes = ts::wireSize<fx::FixedReply>;
	CHECK(ew::writeHeader(context.request, header) == ew::Status::Ok);
	return {rm::GlobalStatus::Ok, ew::headerBytes + header.payloadBytes};
}

Cycle service(Context& context, std::span<const std::uint32_t> ids, bool account, bool timed,
              std::uint64_t& dispatcher)
{
	Cycle result;
	fx::FixedRequest native{};
	native.count = static_cast<std::uint16_t>(ids.size());
	std::copy(ids.begin(), ids.end(), native.ids.begin());
	const auto built = serviceRequest(context, native);
	const auto request = rm::Input{context.request}.first(built.written);
	fx::serviceOwner.fields = context.model;
	fx::serviceOwner.workspace = &context.workspace;
	fx::serviceOwner.batch = &context.batch;
	const auto before = fx::scalarOwner.total(),
	           packBefore = fx::serviceOwner.explicitValueCopyBytes;
	const auto idCopyBefore = fx::serviceOwner.explicitIdCopyBytes;
	const auto start = timed ? Steady::now() : Steady::time_point{};
	const auto processed = en::dispatchEncoded(fx::serviceModel.view(), context.workspace,
	                                           context.capability.session, request, context.reply);
	if (timed)
		dispatcher += elapsed(start);
	CHECK(processed);
	const auto reply = rm::Input{context.reply}.first(processed.written);
	const auto parsed = ew::parse(reply);
	if (!parsed || parsed.header.dispatch != 0 || parsed.header.endpointStatus != 0)
		std::fprintf(stderr, "Service dispatch=%u endpoint=%u executor=%u scratch=%zu used=%zu\n",
		             unsigned(parsed.header.dispatch), unsigned(parsed.header.endpointStatus),
		             unsigned(fx::serviceOwner.last), scratchCapacity, context.workspace.used());
	CHECK(parsed && parsed.header.reply && parsed.header.dispatch == 0 &&
	      parsed.header.endpointStatus == 0);
	fx::FixedReply decoded{};
	CHECK(ts::decode(parsed.payload, decoded) == ts::CodecStatus::Ok &&
	      decoded.count == ids.size());
	for (std::size_t i = 0; i < ids.size(); ++i) {
		const auto& item = decoded.items[i];
		CHECK(item.id == ids[i] && item.status == 0 && item.length == 4 && item.reserved == 0);
		std::array<std::byte, 4> bytes{};
		for (std::size_t j = 0; j < bytes.size(); ++j)
			bytes[j] = std::byte{item.value[j]};
		CHECK(ts::decode(bytes, result.values[i]) == ts::CodecStatus::Ok);
		for (std::size_t j = item.length; j < item.value.size(); ++j)
			CHECK(item.value[j] == 0);
	}
	for (std::size_t i = ids.size(); i < rm::maxItems; ++i) {
		const auto& item = decoded.items[i];
		CHECK(item.id == 0 && item.status == 0 && item.length == 0 && item.reserved == 0);
		for (auto value : item.value)
			CHECK(value == 0);
	}
	result.getterCalls = fx::scalarOwner.total() - before;
	result.servicePackBytes = fx::serviceOwner.explicitValueCopyBytes - packBefore;
	result.serviceIdCopyBytes = fx::serviceOwner.explicitIdCopyBytes - idCopyBefore;
	frames(result, request, reply, account);
	if (account) {
		context.request.fill(std::byte{0xa5});
		context.reply.fill(std::byte{0x5a});
		context.batch = {};
	}
	return result;
}

Cycle capabilityChecks()
{
	const auto before = checks;
	Context context;
	std::array<std::byte, rm::capabilityBytes> record{};
	CHECK(rm::writeCapability(record, context.capability));
	const auto parsed = rm::parseCapability(record);
	CHECK(parsed && parsed.value == context.capability);
	resource::BytesFile file{record};
	const resource::FileSystem<1> files{
	    resource::filesystem(resource::file("/prototype/read-many.cap", file))};
	std::array<std::byte, rp::wire::readRequestSize> request{};
	std::array<std::byte, 64> reply{};
	const auto built = packets::makeRead(request, 0, 0);
	CHECK(built.status == packets::BuildStatus::Ok);
	const auto read = rp::process(files.view(), rm::Input{request}.first(built.written), reply);
	CHECK(read.status == resource::Status::Ok);
	const auto decoded = packets::parseRead(rm::Input{reply}.first(read.written));
	CHECK(decoded && decoded.response.status == resource::Status::Ok && decoded.response.eof);
	CHECK(rm::parseCapability(decoded.response.data).value == context.capability);
	Cycle traffic;
	frames(traffic, rm::Input{request}.first(built.written), rm::Input{reply}.first(read.written),
	       true);
	CHECK(traffic.protocolTx == 13 && traffic.protocolRx == 36 && traffic.framedTx == 19 &&
	      traffic.framedRx == 42);
	for (std::size_t offset : {std::size_t{0}, std::size_t{4}, std::size_t{5}, std::size_t{6}}) {
		auto bad = record;
		bad[offset] = std::byte{0xff};
		CHECK(!rm::parseCapability(bad));
	}
	CHECK(!rm::parseCapability(rm::Input{record}.first(record.size() - 1)));
	for (unsigned which = 0; which < 7; ++which) {
		auto invalid = context.capability;
		if (which == 0)
			invalid.session = 0;
		if (which == 1)
			invalid.itemLimit = 0;
		if (which == 2)
			invalid.itemLimit = 33;
		if (which == 3)
			invalid.requestLimit = 5;
		if (which == 4)
			invalid.replyLimit = 9;
		if (which == 5)
			invalid.requestLimit = 131;
		if (which == 6)
			invalid.replyLimit = 515;
		CHECK(!rm::writeCapability(record, invalid));
		CHECK(!rm::capture(context.metadata.schema, invalid, 1, std::array<std::uint32_t, 1>{0}));
	}
	controls[0].checks = checks - before;
	return traffic;
}

void requestChecks()
{
	const auto before = checks;
	Context context;
	fx::scalarOwner.reset();
	const std::array<std::uint32_t, 2> ids{0, 0x10000u};
	const auto original = rm::writeRequest(context.request, context.capability, 1, ids);
	CHECK(original);
	const auto good = context.request;
	const auto fail = [&](rm::Input request, rm::Output output, rm::GlobalStatus expected,
	                      bool outer) {
		context.batch.count = 27;
		const auto result = rm::dispatch(context.model, context.workspace, context.capability,
		                                 request, output, context.batch);
		CHECK(fx::scalarOwner.total() == 0 && context.batch.count == 27);
		if (outer && (result || result.status != expected || result.written != 0))
			std::fprintf(stderr,
			             "preflight input=%zu output=%zu expected=%u actual=%u written=%zu\n",
			             request.size(), output.size(), unsigned(expected), unsigned(result.status),
			             result.written);
		if (outer)
			CHECK(!result && result.status == expected && result.written == 0);
		else {
			CHECK(result && result.written == rm::headerBytes);
			const auto reply = rm::parseFrame(rm::Input{output}.first(result.written));
			CHECK(reply && reply.value.header.status == expected);
		}
	};
	CHECK(
	    !rm::writeRequest(context.reply, context.capability, 1, std::span<const std::uint32_t>{}));
	std::array<std::uint32_t, 33> oversized{};
	CHECK(!rm::writeRequest(context.reply, context.capability, 1, oversized));
	const std::array<std::uint32_t, 2> duplicate{0, 0};
	CHECK(rm::writeRequest(context.reply, context.capability, 1, duplicate).status ==
	      rm::GlobalStatus::Duplicate);
	for (std::size_t length = 0; length < original.written; ++length)
		fail(rm::Input{good}.first(length), context.reply, rm::GlobalStatus::Malformed, true);
	for (std::size_t offset : {std::size_t{0}, std::size_t{4}, std::size_t{5}, std::size_t{6},
	                           std::size_t{7}, std::size_t{28}}) {
		auto bad = good;
		bad[offset] = std::byte{0xff};
		fail(rm::Input{bad}.first(original.written), context.reply, rm::GlobalStatus::Malformed,
		     true);
	}
	for (std::uint16_t count : {std::uint16_t{0}, std::uint16_t{33}}) {
		auto bad = good;
		rm::detail::store(bad, 32, count);
		fail(rm::Input{bad}.first(original.written), context.reply, rm::GlobalStatus::Malformed,
		     false);
	}
	{
		auto bad = good;
		rm::detail::store(bad, 38, std::uint32_t{0});
		fail(rm::Input{bad}.first(original.written), context.reply, rm::GlobalStatus::Duplicate,
		     false);
	}
	for (std::size_t offset : {std::size_t{8}, std::size_t{16}}) {
		auto bad = good;
		bad[offset] ^=
		    std::byte{2}; // Preserve a nonzero session so admission, not parsing, rejects it.
		fail(rm::Input{bad}.first(original.written), context.reply, rm::GlobalStatus::NotAdmitted,
		     true);
	}
	fail(rm::Input{good}.first(original.written), rm::Output{context.reply}.first(31),
	     rm::GlobalStatus::BufferTooSmall, true);
	fail(rm::Input{good}.first(original.written), rm::Output{context.reply}.first(57),
	     rm::GlobalStatus::BufferTooSmall, false);
	{
		auto bad = good;
		rm::detail::store(bad, 26, std::uint16_t{17});
		fail(rm::Input{bad}.first(original.written), context.reply,
		     rm::GlobalStatus::BufferTooSmall, false);
	}
	context.request = good;
	fail(rm::Input{context.request}.first(original.written), context.request,
	     rm::GlobalStatus::Overlap, true);
	std::copy_n(good.begin(), original.written, context.scratch.begin());
	fail(rm::Input{context.scratch}.first(original.written), context.reply,
	     rm::GlobalStatus::Overlap, true);
	fail(rm::Input{good}.first(original.written), context.scratch, rm::GlobalStatus::Overlap, true);
	{
		auto limited = context.capability;
		limited.itemLimit = 1;
		CHECK(rm::capture(context.metadata.schema, limited, 1, ids).status ==
		      rm::GlobalStatus::UnsupportedProfile);
		const auto result =
		    rm::dispatch(context.model, context.workspace, limited,
		                 rm::Input{good}.first(original.written), context.reply, context.batch);
		CHECK(result && fx::scalarOwner.total() == 0);
		CHECK(rm::parseFrame(rm::Input{context.reply}.first(result.written)).value.header.status ==
		      rm::GlobalStatus::UnsupportedProfile);
	}
	{
		fx::AdaptedModel adapted{&fx::AdaptedModel::scratchRead, 16};
		std::array<std::byte, 0> noStorage{};
		ts::Workspace noScratch{noStorage};
		const auto result =
		    rm::dispatch(adapted.view, noScratch, context.capability,
		                 rm::Input{good}.first(original.written), context.reply, context.batch);
		CHECK(result && fx::scalarOwner.total() == 0);
		CHECK(rm::parseFrame(rm::Input{context.reply}.first(result.written)).value.header.status ==
		      rm::GlobalStatus::ScratchTooSmall);
	}
	{
		fx::AdaptedModel adapted{&fx::AdaptedModel::scratchRead, 16};
		alignas(std::uint64_t) std::array<std::byte, 16> storage{};
		ts::Workspace used{storage};
		ts::Workspace::Lease<std::uint64_t> live{used};
		CHECK(live.valid());
		CHECK(rm::execute(adapted.view, ids, used, context.batch) ==
		      rm::GlobalStatus::ScratchTooSmall);
		CHECK(fx::scalarOwner.total() == 0);
	}
	CHECK(ew::parse(rm::Input{good}.first(original.written)).status == ew::Status::InvalidMagic);
	controls[1].checks = checks - before;
}

void replyChecks()
{
	const auto before = checks;
	Context context;
	const std::array<std::uint32_t, 2> ids{0, 0x10000u};
	const auto captured = rm::capture(context.metadata.schema, context.capability, 1, ids);
	CHECK(captured);
	const auto built = rm::writeRequest(context.request, captured.value, context.capability);
	const auto result =
	    rm::dispatch(context.model, context.workspace, context.capability,
	                 rm::Input{context.request}.first(built.written), context.reply, context.batch);
	CHECK(result && result.written == 58);
	const auto good = context.reply;
	for (std::size_t length = 0; length < result.written; ++length)
		CHECK(
		    !rm::validateReply(rm::Input{good}.first(length), captured.value, context.capability));
	for (std::size_t offset :
	     {std::size_t{0}, std::size_t{4}, std::size_t{5}, std::size_t{7}, std::size_t{8},
	      std::size_t{12}, std::size_t{16}, std::size_t{26}, std::size_t{28}, std::size_t{32},
	      std::size_t{34}, std::size_t{38}, std::size_t{39}, std::size_t{40}, std::size_t{46}}) {
		auto bad = good;
		bad[offset] = std::byte{0xff};
		CHECK(!rm::validateReply(rm::Input{bad}.first(result.written), captured.value,
		                         context.capability));
	}
	{
		auto bad = good;
		bad[result.written] = std::byte{0};
		rm::detail::store(bad, 24, std::uint16_t(result.written + 1 - 32));
		CHECK(!rm::validateReply(rm::Input{bad}.first(result.written + 1), captured.value,
		                         context.capability));
	}
	{
		auto bad = good;
		bad[38] = std::byte(static_cast<std::uint8_t>(rm::ItemStatus::Unavailable));
		CHECK(!rm::validateReply(rm::Input{bad}.first(result.written), captured.value,
		                         context.capability));
	}
	{
		fx::AdaptedModel adapted{&fx::AdaptedModel::partialRead, 0};
		const auto failed = rm::dispatch(adapted.view, context.workspace, context.capability,
		                                 rm::Input{context.request}.first(built.written),
		                                 context.reply, context.batch);
		CHECK(failed);
		const auto parsed = rm::validateReply(rm::Input{context.reply}.first(failed.written),
		                                      captured.value, context.capability);
		CHECK(parsed && parsed.value.count == 2);
		CHECK(parsed.value.items[0].status == rm::ItemStatus::ReadError &&
		      parsed.value.items[0].payload.empty());
		CHECK((context.batch.items[0].length == 0 &&
		       context.batch.items[0].value == std::array<std::byte, 8>{}));
		CHECK(parsed.value.items[1].status == rm::ItemStatus::Ok);
		CHECK(rm::detail::load<std::uint32_t>(parsed.value.items[1].payload, 0) ==
		      read_comparison::data[16]);
	}
	controls[2].checks = checks - before;
}

void identityChecks()
{
	const auto before = checks;
	Context context;
	const std::array<std::uint32_t, 1> ids{0};
	const auto captured = rm::capture(context.metadata.schema, context.capability, 1, ids);
	CHECK(captured);
	auto changed = context.capability;
	++changed.session;
	CHECK(!captured.value.current(changed));
	CHECK(rm::validateReply({}, captured.value, changed).status == rm::GlobalStatus::Stale);
	context.request.fill(std::byte{0xa5});
	const auto unchanged = context.request;
	CHECK(rm::writeRequest(context.request, captured.value, changed).status ==
	      rm::GlobalStatus::Stale);
	CHECK(context.request == unchanged);
	changed = context.capability;
	++changed.fingerprint;
	CHECK(rm::capture(context.metadata.schema, changed, 1, ids).status ==
	      rm::GlobalStatus::NotAdmitted);
	CHECK(rm::writeRequest(context.request, captured.value, changed).status ==
	      rm::GlobalStatus::Stale);
	const auto request = rm::writeRequest(context.request, captured.value, context.capability);
	CHECK(request);
	CHECK(context.metadata.workspace.reset() == telemetry::runtime::Status::Ok);
	CHECK(!captured.value.current(context.capability));
	CHECK(rm::writeRequest(context.request, captured.value, context.capability).status ==
	      rm::GlobalStatus::Stale);
	CHECK(rm::validateReply({}, captured.value, context.capability).status ==
	      rm::GlobalStatus::Stale);
	controls[3].checks = checks - before;
}

void mixedChecks()
{
	const auto before = checks;
	Context context{fx::mixedDescriptorBytes, fx::mixedModel.view()};
	fx::mixedOwner.calls = 0;
	const std::array<std::uint32_t, 1> unknown{0x20000u}, structure{0x10002u};
	CHECK(rm::capture(context.metadata.schema, context.capability, 1, unknown).status ==
	      rm::GlobalStatus::UnknownId);
	CHECK(rm::capture(context.metadata.schema, context.capability, 1, structure).status ==
	      rm::GlobalStatus::UnsupportedType);
	std::array<std::uint32_t, 9> raw{};
	std::copy(fx::mixedIds.begin(), fx::mixedIds.end(), raw.begin());
	raw.back() = unknown[0];
	CHECK(rm::execute(context.model, raw, context.workspace, context.batch) ==
	      rm::GlobalStatus::Ok);
	CHECK(context.batch.count == 9 && fx::mixedOwner.calls == 6);
	CHECK(context.batch.items[6].status == rm::ItemStatus::UnsupportedType &&
	      context.batch.items[6].length == 0);
	CHECK(context.batch.items[7].status == rm::ItemStatus::Unavailable &&
	      context.batch.items[7].length == 0);
	CHECK(context.batch.items[8].status == rm::ItemStatus::NotFound &&
	      context.batch.items[8].length == 0);
	const std::array<std::uint32_t, 6> floats{0x7fc12345u, 0xffc23456u, 0x7f812345u,
	                                          0xff823456u, 0u,          0x80000000u};
	const std::array<std::uint64_t, 6> doubles{0x7ff8123456789abcULL,
	                                           0xfff823456789abcdULL,
	                                           0x7ff0123456789abcULL,
	                                           0xfff023456789abcdULL,
	                                           0ULL,
	                                           0x8000000000000000ULL};
	std::array<std::uint32_t, 7> ids{0, 1, 2, 3, 0x10000u, 0x10001u, 0x10003u};
	for (std::size_t n = 0; n < floats.size(); ++n) {
		fx::mixedOwner.f32bits = floats[n];
		fx::mixedOwner.f64bits = doubles[n];
		const auto captured = rm::capture(context.metadata.schema, context.capability, 1, ids);
		CHECK(captured);
		const auto built = rm::writeRequest(context.request, captured.value, context.capability);
		CHECK(built);
		const auto result = rm::dispatch(context.model, context.workspace, context.capability,
		                                 rm::Input{context.request}.first(built.written),
		                                 context.reply, context.batch);
		CHECK(result);
		const auto parsed = rm::validateReply(rm::Input{context.reply}.first(result.written),
		                                      captured.value, context.capability);
		CHECK(parsed && parsed.value.count == ids.size());
		CHECK(rm::detail::load<std::uint8_t>(parsed.value.items[0].payload, 0) == 1);
		CHECK(rm::detail::load<std::uint64_t>(parsed.value.items[1].payload, 0) == UINT64_MAX);
		CHECK(rm::detail::load<std::uint64_t>(parsed.value.items[2].payload, 0) ==
		      0x8000000000000000ULL);
		CHECK(rm::detail::load<std::uint32_t>(parsed.value.items[3].payload, 0) == floats[n]);
		CHECK(rm::detail::load<std::uint64_t>(parsed.value.items[4].payload, 0) == doubles[n]);
		CHECK(rm::detail::load<std::uint16_t>(parsed.value.items[5].payload, 0) ==
		      std::uint16_t(-123));
		CHECK(parsed.value.items[6].status == rm::ItemStatus::Unavailable &&
		      parsed.value.items[6].payload.empty() && parsed.value.items[6].typeId == 0);
		auto invalidBool = context.reply;
		invalidBool[42] = std::byte{2};
		CHECK(!rm::validateReply(rm::Input{invalidBool}.first(result.written), captured.value,
		                         context.capability));
	}
	fx::FixedRequest native{};
	native.count = static_cast<std::uint16_t>(raw.size());
	std::copy(raw.begin(), raw.end(), native.ids.begin());
	const auto built = serviceRequest(context, native);
	fx::serviceOwner.fields = context.model;
	fx::serviceOwner.workspace = &context.workspace;
	fx::serviceOwner.batch = &context.batch;
	const auto served = en::dispatchEncoded(
	    fx::mixedServiceModel.view(), context.workspace, context.capability.session,
	    rm::Input{context.request}.first(built.written), context.reply);
	CHECK(served);
	const auto parsedService = ew::parse(rm::Input{context.reply}.first(served.written));
	CHECK(parsedService && parsedService.header.dispatch == 0 &&
	      parsedService.header.endpointStatus == 0);
	fx::FixedReply decoded{};
	CHECK(ts::decode(parsedService.payload, decoded) == ts::CodecStatus::Ok &&
	      decoded.count == raw.size());
	for (std::size_t i = 0; i < raw.size(); ++i) {
		const auto& item = decoded.items[i];
		const auto& expected = context.batch.items[i];
		CHECK(item.id == raw[i] && item.status == static_cast<unsigned>(expected.status) &&
		      item.length == expected.length);
		for (std::size_t j = 0; j < rm::maxValueBytes; ++j)
			CHECK(item.value[j] ==
			      (j < item.length ? std::to_integer<unsigned>(expected.value[j]) : 0));
	}
	controls[4].checks = checks - before;
}

void serviceChecks()
{
	const auto before = checks;
	Context context;
	fx::serviceOwner.fields = context.model;
	fx::serviceOwner.workspace = &context.workspace;
	fx::serviceOwner.batch = &context.batch;
	for (std::uint16_t count : {std::uint16_t{0}, std::uint16_t{33}, std::uint16_t{2}}) {
		fx::scalarOwner.reset();
		fx::FixedRequest request{};
		request.count = count;
		const auto built = serviceRequest(context, request);
		const auto result = en::dispatchEncoded(
		    fx::serviceModel.view(), context.workspace, context.capability.session,
		    rm::Input{context.request}.first(built.written), context.reply);
		CHECK(result && fx::scalarOwner.total() == 0);
		const auto parsed = ew::parse(rm::Input{context.reply}.first(result.written));
		CHECK(parsed && parsed.header.dispatch == 0 &&
		      parsed.header.endpointStatus ==
		          static_cast<unsigned>(ts::ServiceStatus::InvalidArgument) &&
		      parsed.payload.empty());
	}
	fx::FixedRequest request{};
	request.count = 1;
	const auto built = serviceRequest(context, request);
	const auto prior = fx::serviceOwner.calls;
	const auto result = en::dispatchEncoded(
	    fx::serviceModel.view(), context.workspace, context.capability.session,
	    rm::Input{context.request}.first(built.written),
	    rm::Output{context.reply}.first(ew::headerBytes + ts::wireSize<fx::FixedReply> - 1));
	CHECK(result && fx::serviceOwner.calls == prior && fx::scalarOwner.total() == 0);
	const auto parsed = ew::parse(rm::Input{context.reply}.first(result.written));
	CHECK(parsed &&
	      parsed.header.dispatch == static_cast<unsigned>(ts::DispatchStatus::BufferTooSmall));
	controls[5].checks = checks - before;
}

Profile profile(std::size_t count, std::uint32_t work, std::uint64_t iterations)
{
	Context context;
	Profile report;
	report.count = count;
	report.getterWork = work;
	report.ids = fx::selection(count);
	const auto ids = std::span<const std::uint32_t>{report.ids}.first(count);
	const auto captured = rm::capture(context.metadata.schema, context.capability, 1, ids);
	CHECK(captured);
	for (std::size_t kind = 0; kind < report.modes.size(); ++kind) {
		auto& mode = report.modes[kind];
		mode.name = kind ? "fixed_service" : "compact";
		fx::scalarOwner.reset(work);
		std::uint64_t unused = 0;
		mode.cycle = kind ? service(context, ids, true, false, unused)
		                  : compact(context, captured.value, true, false, unused);
		CHECK(mode.cycle.getterCalls == count);
		for (std::size_t i = 0; i < count; ++i)
			CHECK(mode.cycle.values[i] == read_comparison::data[fx::linearId(ids[i])]);
		fx::scalarOwner.reset(work);
		const auto start = Steady::now();
		for (std::uint64_t i = 0; i < iterations; ++i) {
			const auto cycle = kind
			                       ? service(context, ids, false, true, mode.dispatcher)
			                       : compact(context, captured.value, false, true, mode.dispatcher);
			mode.checksum += checksum(std::span<const std::uint32_t>{cycle.values}.first(count));
		}
		mode.elapsed = elapsed(start);
		mode.workChecksum = fx::scalarOwner.workChecksum;
		CHECK(fx::scalarOwner.total() == count * iterations);
		CHECK(mode.checksum ==
		      checksum(std::span<const std::uint32_t>{mode.cycle.values}.first(count)) *
		          iterations);
	}
	CHECK(report.modes[0].cycle.values == report.modes[1].cycle.values);
	return report;
}

void valuesJson(std::FILE* file, std::span<const std::uint32_t> values)
{
	std::fputc('[', file);
	for (std::size_t i = 0; i < values.size(); ++i)
		std::fprintf(file, "%s%" PRIu32, i ? "," : "", values[i]);
	std::fputc(']', file);
}

void json(std::FILE* file, const std::array<Profile, 10>& profiles, const Cycle& capability,
          std::uint64_t iterations)
{
	std::fprintf(
	    file,
	    "{\"passed\":true,\"hardware\":false,\"prototype_only\":true,\"production_read_many_implemented\":false,\"device_client_integrated\":false,\"checks\":%" PRIu64
	    ",\"timing_iterations\":%" PRIu64,
	    checks, iterations);
	std::fprintf(file,
	             ",\"capability\":{\"record_bytes\":24,\"protocol_tx_bytes\":%" PRIu64
	             ",\"protocol_rx_bytes\":%" PRIu64 ",\"framed_tx_bytes\":%" PRIu64
	             ",\"framed_rx_bytes\":%" PRIu64 "}",
	             capability.protocolTx, capability.protocolRx, capability.framedTx,
	             capability.framedRx);
	std::fprintf(
	    file,
	    ",\"memory\":{\"capability_sizeof\":%zu,\"capture_sizeof\":%zu,\"batch_sizeof\":%zu,\"metadata_sizeof\":%zu,\"context_sizeof\":%zu,\"fixed_request_wire\":%u,\"fixed_reply_wire\":%u,\"fixed_request_sizeof\":%zu,\"fixed_reply_sizeof\":%zu,\"request_capacity\":2048,\"reply_capacity\":2048,\"workspace_capacity\":%zu}",
	    sizeof(rm::Capability), sizeof(rm::CapturedRequest), sizeof(rm::Batch),
	    sizeof(fx::Metadata), sizeof(Context), unsigned(ts::wireSize<fx::FixedRequest>),
	    unsigned(ts::wireSize<fx::FixedReply>), sizeof(fx::FixedRequest), sizeof(fx::FixedReply),
	    scratchCapacity);
	std::fputs(
	    ",\"timing_note\":\"Instrumented synchronous host elapsed time including validation and timer overhead; no FIFO, transport scheduling or physical latency. Excludes setup, framing and poison.\",\"copy_note\":\"Retained scalar and explicit Service packing/active-ID staging bytes only; not total compiler or Codec copies.\",\"memory_note\":\"ABI-specific object sizes and explicit capacities, not peak stack or firmware RAM/Flash.\",\"controls\":{",
	    file);
	for (std::size_t i = 0; i < controls.size(); ++i)
		std::fprintf(file, "%s\"%s\":%" PRIu64, i ? "," : "", controls[i].name, controls[i].checks);
	std::fputs("},\"profiles\":[", file);
	for (std::size_t p = 0; p < profiles.size(); ++p) {
		const auto& report = profiles[p];
		std::fprintf(file, "%s{\"count\":%zu,\"getter_work\":%" PRIu32 ",\"requested_ids\":",
		             p ? "," : "", report.count, report.getterWork);
		valuesJson(file, std::span<const std::uint32_t>{report.ids}.first(report.count));
		std::fputs(",\"expected_values\":", file);
		valuesJson(
		    file, std::span<const std::uint32_t>{report.modes[0].cycle.values}.first(report.count));
		std::fputs(",\"modes\":[", file);
		for (std::size_t k = 0; k < 2; ++k) {
			const auto& mode = report.modes[k];
			const auto& cycle = mode.cycle;
			std::fprintf(file,
			             "%s{\"name\":\"%s\",\"getter_calls\":%" PRIu64
			             ",\"exchanges\":1,\"protocol_tx_bytes\":%" PRIu64
			             ",\"protocol_rx_bytes\":%" PRIu64 ",\"framed_tx_bytes\":%" PRIu64
			             ",\"framed_rx_bytes\":%" PRIu64
			             ",\"retained_scalar_bytes\":%zu,\"explicit_service_pack_bytes\":%" PRIu64
			             ",\"explicit_service_id_copy_bytes\":%" PRIu64 ",\"owned_values\":",
			             k ? "," : "", mode.name, cycle.getterCalls, cycle.protocolTx,
			             cycle.protocolRx, cycle.framedTx, cycle.framedRx, report.count * 4,
			             cycle.servicePackBytes, cycle.serviceIdCopyBytes);
			valuesJson(file, std::span<const std::uint32_t>{cycle.values}.first(report.count));
			std::fprintf(
			    file,
			    ",\"checksum\":%" PRIu64 ",\"work_checksum\":%" PRIu64
			    ",\"timing\":{\"iterations\":%" PRIu64 ",\"host_elapsed_ns\":%" PRIu64
			    ",\"dispatcher_elapsed_ns\":%" PRIu64 ",\"checksum\":%" PRIu64
			    ",\"framing_included\":false,\"setup_included\":false,\"poison_included\":false}}",
			    checksum(std::span<const std::uint32_t>{cycle.values}.first(report.count)),
			    mode.workChecksum, iterations, mode.elapsed, mode.dispatcher, mode.checksum);
		}
		std::fputs("]}", file);
	}
	std::fputs("]}\n", file);
}
} // namespace

int main(int argc, char** argv)
{
	const char* reportPath = nullptr;
	std::uint64_t iterations = 200;
	for (int i = 1; i < argc; ++i) {
		const std::string_view option{argv[i]};
		if (option == "--report" && i + 1 < argc && !reportPath)
			reportPath = argv[++i];
		else if (option == "--iterations" && i + 1 < argc) {
			const std::string_view text{argv[++i]};
			const auto parsed = std::from_chars(text.data(), text.data() + text.size(), iterations);
			if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() ||
			    iterations == 0 || iterations > 100000)
				return 2;
		} else
			return 2;
	}
	const auto capability = capabilityChecks();
	requestChecks();
	replyChecks();
	identityChecks();
	mixedChecks();
	serviceChecks();
	std::array<Profile, 10> reports{};
	std::size_t position = 0;
	for (std::uint32_t work : {0u, 128u})
		for (std::size_t count :
		     {std::size_t{1}, std::size_t{8}, std::size_t{12}, std::size_t{16}, std::size_t{32}})
			reports[position++] = profile(count, work, iterations);
	if (reportPath) {
		auto* file = std::fopen(reportPath, "wb");
		if (!file)
			return 2;
		json(file, reports, capability, iterations);
		if (std::fclose(file) != 0)
			return 2;
	}
	for (const auto& row : reports)
		std::printf("ReadMany count=%zu work=%" PRIu32 " compact=%" PRIu64 " service=%" PRIu64
		            " getters=%" PRIu64 "\n",
		            row.count, row.getterWork,
		            row.modes[0].cycle.framedTx + row.modes[0].cycle.framedRx,
		            row.modes[1].cycle.framedTx + row.modes[1].cycle.framedRx,
		            row.modes[0].cycle.getterCalls);
	std::printf("ReadMany mapping checks=%" PRIu64 "\n", checks);
}
