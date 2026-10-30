/*
 * @file Snapshot.cpp
 * @brief Guarded snapshot copying, scalar editor paths and caller-buffer preflight.
 * @author Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT
 */
#include "Fixture.hpp"

#include <integration/inspector/SnapshotStore.hpp>

#include <algorithm>
#include <array>
#include <cstring>

using namespace inspector_fixture;

using Store = im::SnapshotStore;
inline constexpr auto packetType = model.typeId<Packet>();
using Payload = std::array<std::byte, telemetry::wireSize<Packet>>;

static Payload payload() noexcept
{
	Payload result{};
	CHECK(telemetry::encode(runtime_fixture::initial, result) == telemetry::CodecStatus::Ok);
	return result;
}

static unsigned scalarPaths(Store& store, const rt::RuntimeSchema& schema,
                            const im::NodeHandle& node) noexcept
{
	const auto info = store.info(node);
	CHECK(info);
	if (info.value.kind == telemetry::TypeKind::Scalar ||
	    info.value.kind == telemetry::TypeKind::Enum) {
		const auto prepared = store.prepareScalar(node);
		CHECK(prepared && schema.accepts(prepared.value));
		CHECK(prepared.value.rootType() == packetType &&
		      prepared.value.leafType() == info.value.type);
		const auto original = store.bytes();
		CHECK(original);
		Payload scratch{}, destination{};
		auto writer = rt::DynamicValueWriter::begin(schema, packetType, original.value, scratch);
		CHECK(writer);
		const auto value = store.scalar(node);
		CHECK(value);
		std::visit(
		    [&](const auto& number) noexcept {
			    const auto status = value.value.enumValue
			                            ? writer.setEnumUnderlying(prepared.value, number)
			                            : writer.set(prepared.value, number);
			    CHECK(status == rt::Status::Ok);
		    },
		    value.value.value);
		CHECK(store.scalar(node, scratch));
		CHECK(store.scalar(node, std::span<const std::byte>{scratch}.first(scratch.size() - 1))
		          .status == im::ModelStatus::InvalidPayload);
		CHECK(writer.commit(destination));
		CHECK(std::equal(original.value.begin(), original.value.end(), destination.begin()));
		return 1;
	}
	CHECK(store.prepareScalar(node).status == im::ModelStatus::TypeMismatch);
	const auto count = store.childCount(node);
	CHECK(count);
	unsigned visited = 0;
	for (std::uint32_t i = 0; i < count.value; ++i) {
		const auto child = store.child(node, i);
		CHECK(child);
		visited += scalarPaths(store, schema, child.value);
	}
	return visited;
}

static void publicationAndPaths() noexcept
{
	Metadata metadata;
	std::array<std::byte, 512> storage{};
	bool admitted = true;
	Store store{storage, {&admitted, +[](const void* context) noexcept {
		                      return *static_cast<const bool*>(context);
	                      }}};
	CHECK(store.admit(metadata.schema, 17) == im::ModelStatus::Ok);
	CHECK(store.select(0, packetType) == im::ModelStatus::Ok);
	CHECK(store.root().status == im::ModelStatus::NoSnapshot);
	const auto original = payload();
	CHECK(store.publish(original, store.admission(), store.revision()) == im::ModelStatus::Ok);
	const auto root = store.root();
	CHECK(root && scalarPaths(store, metadata.schema, root.value) == 34);
	const auto wide = store.child(root.value, 16);
	CHECK(wide);
	const auto prepared = store.prepareScalar(wide.value);
	CHECK(prepared && prepared.value.wireOffset() == original.size() - 8);
	Payload scratch{};
	auto editor = rt::DynamicValueWriter::begin(metadata.schema, packetType, original, scratch);
	CHECK(editor);
	CHECK(editor.setEnumUnderlying(prepared.value, std::uint64_t{UINT64_MAX - 1}) ==
	      rt::Status::Ok);
	const auto staged = store.scalar(wide.value, scratch);
	CHECK(staged && std::get<std::uint64_t>(staged.value.value) == UINT64_MAX - 1 &&
	      !staged.value.enumKnown);
	std::array<char, 20> decimal{};
	const auto text = store.decimal(wide.value, decimal, scratch);
	CHECK(text && std::string_view(decimal.data(), text.value) == "18446744073709551614");
	CHECK(std::get<std::uint64_t>(store.scalar(wide.value).value.value) == UINT64_MAX);

	const auto before = storage;
	auto invalid = original;
	invalid[0] = std::byte{2};
	CHECK(store.publish(invalid, store.admission(), store.revision()) ==
	      im::ModelStatus::InvalidPayload);
	CHECK(storage == before && store.snapshotInfo().ready);
	CHECK(store.publish(std::span<const std::byte>{original}.first(original.size() - 1),
	                    store.admission(), store.revision()) == im::ModelStatus::InvalidPayload);
	CHECK(storage == before);

	std::array<std::byte, 512> otherBytes{};
	Store other{otherBytes};
	CHECK(other.admit(metadata.schema, 17) == im::ModelStatus::Ok);
	CHECK(other.select(0, packetType) == im::ModelStatus::Ok);
	CHECK(other.publish(original, other.admission(), other.revision()) == im::ModelStatus::Ok);
	CHECK(other.info(root.value).status == im::ModelStatus::Stale);
	CHECK(other.prepareScalar(wide.value).status == im::ModelStatus::Stale);

	const auto oldRevision = store.revision();
	CHECK(store.select(0, packetType) == im::ModelStatus::Ok);
	CHECK(!store.info(root.value) && !store.prepareScalar(wide.value));
	CHECK(store.publish(original, store.admission(), oldRevision) == im::ModelStatus::Stale);
	CHECK(!store.snapshotInfo().ready && storage == before);
	CHECK(store.publish(original, store.admission(), store.revision()) == im::ModelStatus::Ok);
	const auto currentRoot = store.root();
	CHECK(currentRoot);
	const auto currentLeaf = store.child(currentRoot.value, 7);
	CHECK(currentLeaf);
	admitted = false;
	CHECK(!store.isCurrent() && !store.root() && !store.bytes());
	CHECK(store.scalar(currentLeaf.value).status == im::ModelStatus::Stale);
	CHECK(store.prepareScalar(currentLeaf.value).status == im::ModelStatus::Stale);
	CHECK(!store.scalar(currentLeaf.value, {}));
	CHECK(storage == before);
}

static void storageAndGeneration() noexcept
{
	Metadata metadata;
	auto typesBefore = metadata.types;
	Store overlapping{std::as_writable_bytes(std::span{metadata.types})};
	CHECK(overlapping.admit(metadata.schema, 17) == im::ModelStatus::InvalidPayload);
	CHECK(std::memcmp(typesBefore.data(), metadata.types.data(), sizeof(typesBefore)) == 0);

	std::array<std::byte, 512> storage{};
	Store store{storage};
	CHECK(store.admit(metadata.schema, 17) == im::ModelStatus::Ok);
	CHECK(store.select(0, packetType) == im::ModelStatus::Ok);
	const auto original = payload();
	std::copy(original.begin(), original.end(), storage.begin() + 4);
	CHECK(store.publish(std::span<const std::byte>{storage}.subspan(4, original.size()),
	                    store.admission(), store.revision()) == im::ModelStatus::Ok);
	CHECK(std::equal(original.begin(), original.end(), storage.begin()));
	const auto root = store.root();
	CHECK(root);
	const auto leaf = store.child(root.value, 7);
	CHECK(leaf);
	CHECK(metadata.workspace.reset() == rt::Status::Ok);
	CHECK(!store.isCurrent() && !store.bytes() && !store.info(root.value));
	CHECK(!store.prepareScalar(leaf.value) && !store.scalar(leaf.value, {}));
	CHECK(store.admit(metadata.schema, 18) == im::ModelStatus::Stale);
}

int main()
{
	publicationAndPaths();
	storageAndGeneration();
	std::printf("Inspector SnapshotStore: %u checks, 0 failures\n", checks);
}
