/*
 * @file Writer.cpp
 * @brief Independent transactional wire edits, scalar bit patterns and alias refusals.
 * SPDX-License-Identifier: MIT
 */
#include <telemetry/runtime/DynamicValueWriter.hpp>

#include <array>
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <type_traits>
#include <utility>

#ifdef RUNTIME_EXPECT_BIG_ENDIAN
static_assert(std::endian::native == std::endian::big);
#endif

namespace {
namespace rt = telemetry::runtime;
unsigned checks = 0;

void require(bool condition, int line)
{
	++checks;
	if (!condition) {
		std::fprintf(stderr, "runtime writer check %u failed at line %d\n", checks, line);
		std::exit(1);
	}
}

#define CHECK(...) require(bool(__VA_ARGS__), __LINE__)

struct Storage {
	std::array<rt::RuntimeType, 128> types{};
	std::array<rt::RuntimeMember, 128> members{};
	std::array<rt::RuntimeEnumEntry, 32> enums{};
	std::array<rt::RuntimeCatalog, 16> catalogs{};
	std::array<rt::RuntimeEndpoint, 32> endpoints{};
	rt::RuntimeSchemaWorkspace workspace{types, members, enums, catalogs, endpoints};
};

template<std::size_t N>
std::size_t load(const char* root, const char* name, std::array<std::byte, N>& out)
{
	char path[4096];
	CHECK(std::snprintf(path, sizeof(path), "%s/%s", root, name) < int(sizeof(path)));
	auto* file = std::fopen(path, "rb");
	CHECK(file);
	const auto size = std::fread(out.data(), 1, out.size(), file);
	CHECK(std::fgetc(file) == EOF);
	CHECK(std::fclose(file) == 0);
	return size;
}

// This oracle encodes directly from the public scalar representation. It uses
// neither Writer helpers nor the runtime Reader or Native codec implementation.
template<class T>
auto littleEndian(const T& value)
{
	std::array<std::byte, sizeof(T)> result{};
	if constexpr (std::is_same_v<T, bool>) {
		result[0] = value ? std::byte{1} : std::byte{0};
	} else {
		using Bits = std::conditional_t<
		    sizeof(T) == 1, std::uint8_t,
		    std::conditional_t<sizeof(T) == 2, std::uint16_t,
		                       std::conditional_t<sizeof(T) == 4, std::uint32_t, std::uint64_t>>>;
		const Bits bits = std::bit_cast<Bits>(value);
		for (std::size_t i = 0; i < sizeof(T); ++i)
			result[i] = std::byte((bits >> (8 * i)) & 255);
	}
	return result;
}

rt::PreparedHandle resolve(const rt::RuntimeSchema& schema, telemetry::TypeId root,
                           std::string_view path)
{
	const auto result = schema.resolve(root, path);
	CHECK(result);
	return result.value;
}

template<class T>
void scalar(const rt::RuntimeSchema& schema, telemetry::TypeId type, const T& value)
{
	alignas(8) std::array<std::byte, 19> original{}, scratch{}, output{};
	original.fill(std::byte{0x31});
	scratch.fill(std::byte{0xa5});
	output.fill(std::byte{0x5a});
	// Both the scalar itself and the transaction buffers are deliberately unaligned.
	if constexpr (std::is_same_v<T, bool>)
		original[1] = std::byte{0};
	const auto old = original;
	const auto handle = resolve(schema, type, "");
	auto writer =
	    rt::DynamicValueWriter::begin(schema, type, {original.data() + 1, sizeof(T)},
	                                  std::span<std::byte>{scratch}.subspan(1, sizeof(T) + 2));
	CHECK(writer);
	CHECK(writer.status() == rt::Status::Ok);
	CHECK(std::memcmp(scratch.data() + 1, original.data() + 1, sizeof(T)) == 0);
	CHECK(writer.set(handle, value) == rt::Status::Ok);
	const auto expected = littleEndian(value);
	CHECK(std::memcmp(scratch.data() + 1, expected.data(), sizeof(T)) == 0);
	CHECK(scratch.front() == std::byte{0xa5});
	CHECK(scratch[sizeof(T) + 1] == std::byte{0xa5});
	CHECK(scratch[sizeof(T) + 2] == std::byte{0xa5});
	const auto committed = writer.commit({output.data() + 1, sizeof(T)});
	CHECK(committed);
	CHECK(committed.value == sizeof(T));
	CHECK(std::memcmp(output.data() + 1, expected.data(), sizeof(T)) == 0);
	CHECK(output.front() == std::byte{0x5a});
	CHECK(output[sizeof(T) + 1] == std::byte{0x5a});
	CHECK(original == old);
	CHECK(!writer);
	CHECK(writer.status() == rt::Status::Stale);
	const auto once = output;
	CHECK(writer.commit({output.data() + 1, sizeof(T)}).status == rt::Status::Stale);
	CHECK(writer.set(handle, value) == rt::Status::Stale);
	CHECK(output == once);
}

void scalars(const rt::RuntimeSchema& schema)
{
	scalar(schema, 1, false);
	scalar(schema, 1, true);
	auto integer = [&]<class T>(telemetry::TypeId type) {
		scalar(schema, type, T{0});
		scalar(schema, type, T{1});
		scalar(schema, type, std::numeric_limits<T>::max());
		if constexpr (std::is_signed_v<T>) {
			scalar(schema, type, T{-1});
			scalar(schema, type, std::numeric_limits<T>::min());
		}
	};
	integer.operator()<std::uint8_t>(2);
	integer.operator()<std::int8_t>(3);
	integer.operator()<std::uint16_t>(4);
	integer.operator()<std::int16_t>(5);
	integer.operator()<std::uint32_t>(6);
	integer.operator()<std::int32_t>(7);
	integer.operator()<std::uint64_t>(8);
	integer.operator()<std::int64_t>(9);
	// Signed zeros, both subnormal endpoints, normal extrema, infinities and
	// positive/negative quiet AND signaling NaNs must remain byte-exact.
	for (auto bits :
	     {UINT32_C(0), UINT32_C(0x80000000), UINT32_C(1), UINT32_C(0x80000001),
	      UINT32_C(0x007fffff), UINT32_C(0x00800000), UINT32_C(0x7f7fffff), UINT32_C(0xff7fffff),
	      UINT32_C(0x7f800000), UINT32_C(0xff800000), UINT32_C(0x7fc12345), UINT32_C(0xffc12345),
	      UINT32_C(0x7f812345), UINT32_C(0xff812345)}) {
		const auto value = std::bit_cast<float>(bits);
		CHECK(std::bit_cast<std::uint32_t>(value) == bits);
		scalar(schema, 10, value);
		CHECK(std::bit_cast<std::uint32_t>(value) == bits);
	}
	for (auto bits :
	     {UINT64_C(0), UINT64_C(0x8000000000000000), UINT64_C(1), UINT64_C(0x8000000000000001),
	      UINT64_C(0x000fffffffffffff), UINT64_C(0x0010000000000000), UINT64_C(0x7fefffffffffffff),
	      UINT64_C(0xffefffffffffffff), UINT64_C(0x7ff0000000000000), UINT64_C(0xfff0000000000000),
	      UINT64_C(0x7ff8123456789abc), UINT64_C(0xfff8123456789abc), UINT64_C(0x7ff0123456789abc),
	      UINT64_C(0xfff0123456789abc)}) {
		const auto value = std::bit_cast<double>(bits);
		CHECK(std::bit_cast<std::uint64_t>(value) == bits);
		scalar(schema, 11, value);
		CHECK(std::bit_cast<std::uint64_t>(value) == bits);
	}
}

void packetEdits(const rt::RuntimeSchema& schema, telemetry::TypeId root, rt::Input payload)
{
	std::array<std::byte, 4096> scratch{}, output{}, expected{};
	std::memcpy(expected.data(), payload.data(), payload.size());
	auto writer = rt::DynamicValueWriter::begin(schema, root, payload, scratch);
	CHECK(writer);
	auto edit = [&]<class T>(std::string_view path, const T& value) {
		const auto h = resolve(schema, root, path);
		const auto bytes = littleEndian(value);
		CHECK(writer.set(h, value) == rt::Status::Ok);
		std::memcpy(expected.data() + h.wireOffset(), bytes.data(), bytes.size());
	};
	edit("flag", false);
	edit("nested.code", std::uint16_t{0xabcd});
	edit("rows[0].bins[2]", std::int8_t{-91});
	edit("rows[1].ready", false);
	edit("matrix[1][0]", std::uint32_t{0x78563412});
	// Repeated edits overwrite only the selected leaf, with last edit winning.
	edit("nested.code", std::uint16_t{0x5678});
	const auto mode = resolve(schema, root, "rows[1].mode");
	CHECK(writer.setEnumUnderlying(mode, std::int16_t{-32768}) == rt::Status::Ok);
	const auto modeBytes = littleEndian(std::int16_t{-32768});
	std::memcpy(expected.data() + mode.wireOffset(), modeBytes.data(), modeBytes.size());
	const auto wide = resolve(schema, root, "wide");
	CHECK(writer.setEnumUnderlying(wide, std::uint64_t{0x0123456789abcdef}) == rt::Status::Ok);
	const auto wideBytes = littleEndian(std::uint64_t{0x0123456789abcdef});
	std::memcpy(expected.data() + wide.wireOffset(), wideBytes.data(), wideBytes.size());
	const auto result = writer.commit(std::span{output}.first(payload.size()));
	CHECK(result && result.value == payload.size());
	CHECK(std::memcmp(output.data(), expected.data(), payload.size()) == 0);
	const auto read = schema.view(root, {output.data(), payload.size()});
	CHECK(read);
	CHECK(!read.value.enumName(mode).value.known);
	CHECK(!read.value.enumName(wide).value.known);
	// Original may be the destination: begin already owns its complete snapshot.
	std::array<std::byte, 4096> original{};
	std::memcpy(original.data(), payload.data(), payload.size());
	auto inPlace =
	    rt::DynamicValueWriter::begin(schema, root, {original.data(), payload.size()}, scratch);
	CHECK(inPlace.set(resolve(schema, root, "u32"), std::uint32_t{0x01020304}) == rt::Status::Ok);
	CHECK(inPlace.commit(std::span{original}.first(payload.size())));
	CHECK(original[resolve(schema, root, "u32").wireOffset()] == std::byte{4});
	// No pointer to the source may be consulted after begin returns.
	auto sourceEnded = [&] {
		std::array<std::byte, 4096> temporary{};
		std::memcpy(temporary.data(), payload.data(), payload.size());
		return rt::DynamicValueWriter::begin(schema, root, {temporary.data(), payload.size()},
		                                     scratch);
	}();
	CHECK(sourceEnded);
	CHECK(sourceEnded.commit(std::span{output}.first(payload.size())));
	CHECK(std::memcmp(output.data(), payload.data(), payload.size()) == 0);
}

void crossLanguage(const char* directory, const rt::RuntimeSchema& schema, telemetry::TypeId root,
                   rt::Input payload)
{
	std::array<std::byte, 4096> scratch{}, output{};
	auto writer = rt::DynamicValueWriter::begin(schema, root, payload, scratch);
	CHECK(writer);
	CHECK(writer.set(resolve(schema, root, "flag"), false) == rt::Status::Ok);
	CHECK(writer.set(resolve(schema, root, "u32"), std::uint32_t{115200}) == rt::Status::Ok);
	const auto nan = std::bit_cast<float>(UINT32_C(0x7fc12345));
	CHECK(writer.set(resolve(schema, root, "f32"), nan) == rt::Status::Ok);
	CHECK(writer.setEnumUnderlying(resolve(schema, root, "nested.mode"), std::int16_t{-2}) ==
	      rt::Status::Ok);
	CHECK(writer.commit(std::span{output}.first(payload.size())));
	char path[4096];
	CHECK(std::snprintf(path, sizeof(path), "%s/writer.bin", directory) < int(sizeof(path)));
	auto* file = std::fopen(path, "wb");
	CHECK(file);
	CHECK(std::fwrite(output.data(), 1, payload.size(), file) == payload.size());
	CHECK(std::fclose(file) == 0);
}

void failures(const rt::RuntimeSchema& schema, telemetry::TypeId root, rt::Input payload,
              const rt::RuntimeSchema& foreign)
{
	std::array<std::byte, 4096> scratch{}, output{};
	const auto flag = resolve(schema, root, "flag");
	const auto mode = resolve(schema, root, "nested.mode");
	auto refusal = [&](const auto& operation, rt::Status expected) {
		scratch.fill(std::byte{0x55});
		output.fill(std::byte{0xa7});
		auto writer = rt::DynamicValueWriter::begin(schema, root, payload, scratch);
		CHECK(writer);
		const auto staging = scratch;
		const auto destination = output;
		CHECK(operation(writer) == expected);
		CHECK(writer.status() == expected);
		CHECK(!writer);
		CHECK(scratch == staging);
		// Ignore the first error deliberately: later valid operations cannot publish.
		CHECK(writer.set(flag, false) == expected);
		CHECK(writer.commit(std::span{output}.first(payload.size())).status == expected);
		CHECK(scratch == staging);
		CHECK(output == destination);
	};
	refusal(
	    [&](auto& w) {
		    return w.set(flag, std::uint8_t{1});
	    },
	    rt::Status::TypeMismatch);
	refusal(
	    [&](auto& w) {
		    return w.set(mode, std::int16_t{12});
	    },
	    rt::Status::TypeMismatch);
	refusal(
	    [&](auto& w) {
		    return w.setEnumUnderlying(mode, std::uint16_t{12});
	    },
	    rt::Status::TypeMismatch);
	refusal(
	    [&](auto& w) {
		    return w.setEnumUnderlying(flag, false);
	    },
	    rt::Status::TypeMismatch);
	refusal(
	    [&](auto& w) {
		    return w.set(rt::PreparedHandle{}, false);
	    },
	    rt::Status::Stale);
	refusal(
	    [&](auto& w) {
		    return w.set(resolve(foreign, root, "flag"), false);
	    },
	    rt::Status::Stale);
	refusal(
	    [&](auto& w) {
		    return w.set(resolve(schema, 1, ""), false);
	    },
	    rt::Status::TypeMismatch);
	refusal(
	    [&](auto& w) {
		    return w.set(resolve(schema, root, "nested"), std::uint16_t{1});
	    },
	    rt::Status::TypeMismatch);
	for (auto length : {payload.size() - 1, payload.size() + 1}) {
		auto writer = rt::DynamicValueWriter::begin(schema, root, payload, scratch);
		output.fill(std::byte{0x37});
		const auto destination = output;
		CHECK(writer.commit(std::span{output}.first(length)).status == rt::Status::InvalidPayload);
		CHECK(output == destination);
		CHECK(writer.commit(std::span{output}.first(payload.size())).status ==
		      rt::Status::InvalidPayload);
		CHECK(output == destination);
	}
	CHECK(rt::DynamicValueWriter::begin(schema, root, payload,
	                                    std::span{scratch}.first(payload.size() - 1))
	          .status() == rt::Status::Capacity);
	CHECK(rt::DynamicValueWriter::begin(schema, root, payload.first(payload.size() - 1), scratch)
	          .status() == rt::Status::InvalidPayload);
	CHECK(rt::DynamicValueWriter::begin(schema, 99999, {}, scratch).status() ==
	      rt::Status::TypeNotFound);
	CHECK(rt::DynamicValueWriter::begin(schema, 0, {}, scratch).status() ==
	      rt::Status::TypeMismatch);
	std::array<std::byte, 4096> bad{};
	std::memcpy(bad.data(), payload.data(), payload.size());
	bad[flag.wireOffset()] = std::byte{2};
	scratch.fill(std::byte{0xa1});
	const auto untouched = scratch;
	CHECK(rt::DynamicValueWriter::begin(schema, root, {bad.data(), payload.size()}, scratch)
	          .status() == rt::Status::InvalidPayload);
	CHECK(scratch == untouched);
	bad[flag.wireOffset()] = std::byte{1};
	bad[resolve(schema, root, "rows[1].ready").wireOffset()] = std::byte{255};
	CHECK(rt::DynamicValueWriter::begin(schema, root, {bad.data(), payload.size()}, scratch)
	          .status() == rt::Status::InvalidPayload);
	CHECK(scratch == untouched);
}

void finalValidation(const rt::RuntimeSchema& schema, telemetry::TypeId root, rt::Input payload)
{
	std::array<std::byte, 4096> scratch{}, output{};
	output.fill(std::byte{0xd3});
	const auto destination = output;
	const auto flag = resolve(schema, root, "flag");
	const auto f32 = resolve(schema, root, "f32");
	const auto nan = std::bit_cast<float>(UINT32_C(0xff812345));
	CHECK(std::bit_cast<std::uint32_t>(nan) == UINT32_C(0xff812345));
	auto nanWriter = rt::DynamicValueWriter::begin(schema, root, payload, scratch);
	CHECK(nanWriter.set(f32, nan) == rt::Status::Ok);
	CHECK(std::bit_cast<std::uint32_t>(nan) == UINT32_C(0xff812345));
	const auto encodedNaN = littleEndian(nan);
	CHECK(std::memcmp(scratch.data() + f32.wireOffset(), encodedNaN.data(), encodedNaN.size()) ==
	      0);
	const auto nanStaging = scratch;
	CHECK(nanWriter.set(flag, std::uint8_t{0}) == rt::Status::TypeMismatch);
	CHECK(scratch == nanStaging);
	CHECK(nanWriter.commit(std::span{output}.first(payload.size())).status ==
	      rt::Status::TypeMismatch);
	CHECK(scratch == nanStaging && output == destination);

	auto writer = rt::DynamicValueWriter::begin(schema, root, payload, scratch);
	CHECK(writer);
	const auto nestedBool = resolve(schema, root, "rows[1].ready");
	// Intentional sequential contract violation, solely to exercise commit's
	// final canonical-value validation. Applications never mutate staging.
	scratch[nestedBool.wireOffset()] = std::byte{2};
	const auto malformed = scratch;
	const auto refused = writer.commit(std::span{output}.first(payload.size()));
	CHECK(refused.status == rt::Status::InvalidPayload && refused.value == 0);
	CHECK(scratch == malformed && output == destination);
	// Repairing that byte cannot erase the already observed sticky failure.
	scratch[nestedBool.wireOffset()] = std::byte{1};
	CHECK(writer.commit(std::span{output}.first(payload.size())).status ==
	      rt::Status::InvalidPayload);
	CHECK(output == destination);
}

void aliases(const rt::RuntimeSchema& schema, Storage& storage, std::span<std::byte> descriptor)
{
	std::array<std::byte, 16> source{}, scratch{}, output{};
	source.fill(std::byte{0x42});
	auto checkRegion = [&](std::span<std::byte> region) {
		CHECK(!region.empty());
		const auto first = region.front();
		CHECK(
		    rt::DynamicValueWriter::begin(schema, 2, std::span{source}.first(1), region).status() ==
		    rt::Status::Overlap);
		CHECK(region.front() == first);
		// A source alias is read-only and therefore allowed; the snapshot must
		// retain that byte without modifying published schema storage.
		auto snapshot =
		    rt::DynamicValueWriter::begin(schema, 2, rt::Input{region.data(), 1}, scratch);
		CHECK(snapshot);
		CHECK(snapshot.commit(std::span{output}.first(1)));
		CHECK(output.front() == first);
		CHECK(region.front() == first);
		auto writer = rt::DynamicValueWriter::begin(schema, 2, std::span{source}.first(1), scratch);
		CHECK(writer);
		CHECK(writer.commit(region.first(1)).status == rt::Status::Overlap);
		CHECK(region.front() == first);
		const auto old = output;
		CHECK(writer.commit(std::span{output}.first(1)).status == rt::Status::Overlap);
		CHECK(output == old);
	};
	checkRegion(descriptor);
	checkRegion(std::as_writable_bytes(std::span{storage.types}));
	checkRegion(std::as_writable_bytes(std::span{storage.members}));
	checkRegion(std::as_writable_bytes(std::span{storage.enums}));
	checkRegion(std::as_writable_bytes(std::span{storage.catalogs}));
	checkRegion(std::as_writable_bytes(std::span{storage.endpoints}));
	checkRegion(std::as_writable_bytes(std::span{&storage.workspace, 1}));
	CHECK(rt::DynamicValueWriter::begin(schema, 2, std::span{source}.first(1), source).status() ==
	      rt::Status::Overlap);
	// Overlap is forbidden even if pointers differ; adjacent spans are legal.
	CHECK(rt::DynamicValueWriter::begin(schema, 4, std::span{source}.first(2),
	                                    std::span{source}.subspan(1, 2))
	          .status() == rt::Status::Overlap);
	auto adjacent = rt::DynamicValueWriter::begin(schema, 4, std::span{source}.first(2),
	                                              std::span{source}.subspan(2, 2));
	CHECK(adjacent);
	CHECK(adjacent.commit(std::span{output}.first(2)));
	auto writer = rt::DynamicValueWriter::begin(schema, 4, std::span{source}.first(2), scratch);
	const auto staging = scratch;
	CHECK(writer.commit(std::span{scratch}.subspan(1, 2)).status == rt::Status::Overlap);
	CHECK(scratch == staging);
}

void movesAndInvalidation(Storage& storage, const rt::RuntimeSchema& schema, telemetry::TypeId root,
                          rt::Input payload)
{
	static_assert(!std::is_copy_constructible_v<rt::DynamicValueWriter>);
	static_assert(!std::is_copy_assignable_v<rt::DynamicValueWriter>);
	static_assert(std::is_nothrow_move_constructible_v<rt::DynamicValueWriter>);
	static_assert(std::is_nothrow_move_assignable_v<rt::DynamicValueWriter>);
	std::array<std::byte, 4096> scratch{}, otherScratch{}, output{};
	const auto flag = resolve(schema, root, "flag");
	auto first = rt::DynamicValueWriter::begin(schema, root, payload, scratch);
	CHECK(first.set(flag, false) == rt::Status::Ok);
	auto second = std::move(first);
	CHECK(!first && first.status() == rt::Status::Stale);
	CHECK(first.set(flag, true) == rt::Status::Stale);
	CHECK(first.commit(std::span{output}.first(payload.size())).status == rt::Status::Stale);
	CHECK(second);
	auto destination = rt::DynamicValueWriter::begin(schema, root, payload, otherScratch);
	destination = std::move(second);
	CHECK(!second && second.status() == rt::Status::Stale);
	CHECK(destination.commit(std::span{output}.first(payload.size())));
	CHECK(output[flag.wireOffset()] == std::byte{0});
	// An abandoned transaction never writes its original or any destination.
	const auto once = output;
	{
		auto abandoned = rt::DynamicValueWriter::begin(schema, root, payload, scratch);
		CHECK(abandoned.set(flag, false) == rt::Status::Ok);
	}
	CHECK(output == once);
	auto clean = rt::DynamicValueWriter::begin(schema, root, payload, scratch);
	auto sticky = rt::DynamicValueWriter::begin(schema, root, payload, otherScratch);
	CHECK(sticky.set(flag, std::uint8_t{0}) == rt::Status::TypeMismatch);
	CHECK(storage.workspace.reset() == rt::Status::Ok);
	const auto staging = scratch;
	const auto destinationBefore = output;
	CHECK(!clean && clean.status() == rt::Status::Stale);
	CHECK(clean.set(flag, true) == rt::Status::Stale);
	CHECK(clean.commit(std::span{output}.first(payload.size())).status == rt::Status::Stale);
	CHECK(scratch == staging && output == destinationBefore);
	CHECK(sticky.status() == rt::Status::TypeMismatch);
	CHECK(sticky.commit(std::span{output}.first(payload.size())).status ==
	      rt::Status::TypeMismatch);
	CHECK(output == destinationBefore);
	CHECK(rt::DynamicValueWriter::begin(schema, root, payload, scratch).status() ==
	      rt::Status::Stale);
}

void execute(const char* directory)
{
	std::array<std::byte, 65536> descriptor{};
	std::array<std::byte, 4096> payload{};
	const auto descriptorSize = load(directory, "descriptor.bin", descriptor);
	const auto payloadSize = load(directory, "payload.bin", payload);
	Storage storage, foreignStorage;
	const auto parsed =
	    rt::RuntimeSchema::parse({descriptor.data(), descriptorSize}, storage.workspace);
	const auto foreign =
	    rt::RuntimeSchema::parse({descriptor.data(), descriptorSize}, foreignStorage.workspace);
	CHECK(parsed && foreign);
	const auto& schema = parsed.schema;
	const auto* endpoint = schema.findField("dynamic", "Packet");
	CHECK(endpoint);
	const auto root = endpoint->typeId;
	const rt::Input original{payload.data(), payloadSize};
	const auto preserved = payload;
	scalars(schema);
	packetEdits(schema, root, original);
	crossLanguage(directory, schema, root, original);
	failures(schema, root, original, foreign.schema);
	finalValidation(schema, root, original);
	aliases(schema, storage, std::span{descriptor}.first(descriptorSize));
	for (auto name : {"Empty", "Zero", "HugeZero"}) {
		const auto* zero = schema.findField("dynamic", name);
		CHECK(zero);
		std::array<std::byte, 3> scratch{std::byte{0x51}, std::byte{0x52}, std::byte{0x53}};
		const auto prior = scratch;
		auto writer = rt::DynamicValueWriter::begin(schema, zero->typeId, {}, scratch);
		CHECK(writer);
		const auto result = writer.commit({});
		CHECK(result && result.value == 0);
		CHECK(scratch == prior);
		CHECK(writer.status() == rt::Status::Stale);
	}
	movesAndInvalidation(storage, schema, root, original);
	CHECK(payload == preserved);
}
} // namespace

int main(int argc, char** argv)
{
	if (argc != 2)
		return 2;
	execute(argv[1]);
	std::printf("runtime writer: %u checks, 0 failures\n", checks);
	return 0;
}
