/* Released schema storage, sealed revisions and callback ownership. MIT. */
#include "Fixture.hpp"

#include <memory>

using namespace inspector_fixture;
namespace dy = telemetry::integration::dynamic;

struct Storage {
	std::unique_ptr<std::byte[]> descriptor{new std::byte[bytes.size()]};
	std::unique_ptr<rt::RuntimeType[]> types{new rt::RuntimeType[64]{}};
	std::unique_ptr<rt::RuntimeMember[]> members{new rt::RuntimeMember[128]{}};
	std::unique_ptr<rt::RuntimeEnumEntry[]> enums{new rt::RuntimeEnumEntry[16]{}};
	std::unique_ptr<rt::RuntimeCatalog[]> catalogs{new rt::RuntimeCatalog[8]{}};
	std::unique_ptr<rt::RuntimeEndpoint[]> endpoints{new rt::RuntimeEndpoint[32]{}};
	rt::RuntimeSchemaWorkspace workspace{{types.get(), 64},
	                                     {members.get(), 128},
	                                     {enums.get(), 16},
	                                     {catalogs.get(), 8},
	                                     {endpoints.get(), 32}};
	rt::RuntimeSchema schema{};

	Storage() noexcept
	{
		std::copy(bytes.begin(), bytes.end(), descriptor.get());
		const auto result = rt::RuntimeSchema::parse({descriptor.get(), bytes.size()}, workspace);
		CHECK(result);
		schema = result.schema;
	}

	void release() noexcept
	{
		descriptor.reset();
		types.reset();
		members.reset();
		enums.reset();
		catalogs.reset();
		endpoints.reset();
	}
};

template<class Fixture>
static void admitExternal(Fixture& fixture, Storage& storage) noexcept
{
	auto& model = fixture.inspector;
	model.invalidate();
	model.process(fixture.now);
	CHECK(model.pending() == 0);
	CHECK(model.confirmQuiescence({model.session(), true, true, true}));
	CHECK(model.admit(storage.schema, {storage.descriptor.get(), 64}, 18) == cl::Status::Ok);
}

template<class Model>
static void stale(Model& model, const im::NodeHandle& root, const im::NodeHandle& leaf) noexcept
{
	CHECK(!model.isCurrent());
	CHECK(model.root().status == im::ModelStatus::Stale);
	CHECK(model.info(root).status == im::ModelStatus::Stale);
	CHECK(model.childCount(root).status == im::ModelStatus::Stale);
	CHECK(model.child(root, 0).status == im::ModelStatus::Stale);
	CHECK(model.scalar(leaf).status == im::ModelStatus::Stale);
	std::array<char, 32> output{};
	output.fill('q');
	CHECK(model.decimal(leaf, output).status == im::ModelStatus::Stale);
	CHECK(std::all_of(output.begin(), output.end(), [](char c) {
		return c == 'q';
	}));
	CHECK(model.fields().empty() && model.commands().empty() && model.services().empty());
	CHECK(model.catalogs(telemetry::descriptor::Category::Field).empty());
	CHECK(model.catalogs(telemetry::descriptor::Category::Command).empty());
	CHECK(model.catalogs(telemetry::descriptor::Category::Service).empty());
	CHECK(!model.bindField(0));
	CHECK(!model.snapshotInfo().ready);
}

static void releasedMetadata(bool explicitInvalidate) noexcept
{
	Encoded fixture;
	Storage storage;
	admitExternal(fixture, storage);
	auto& model = fixture.inspector;
	CHECK(model.readField(fixture.bind(), [](const auto& result) noexcept {
		CHECK(result.published);
	}));
	fixture.pump();
	const auto root = model.root();
	CHECK(root);
	const auto leaf = child(model, root.value, 7);
	CHECK(scalar<std::uint64_t>(model, leaf) == UINT64_MAX);
	if (explicitInvalidate)
		model.invalidate();
	else
		CHECK(storage.workspace.reset() == rt::Status::Ok);
	// Descriptor and all five index arrays are genuinely deallocated. Only the
	// required workspace control object remains alive for generation validation.
	storage.release();
	stale(model, root.value, leaf);
	model.process(fixture.now);
	CHECK(model.pending() == 0);
	model.close();
	model.process(fixture.now);
	CHECK(model.drained());
}

static void releasedPending(bool prepared, bool explicitInvalidate) noexcept
{
	Encoded fixture;
	Storage storage;
	admitExternal(fixture, storage);
	auto& model = fixture.inspector;
	unsigned calls = 0;
	CHECK(model.readField(fixture.bind(), [&](const auto& result) noexcept {
		++calls;
		CHECK(result.operation.status == (prepared ? cl::Status::Ok : cl::Status::Cancelled));
		CHECK(result.valueStatus == dy::ValueStatus::Stale);
		CHECK(result.modelStatus == im::ModelStatus::Stale && !result.published);
		CHECK(model.fields().empty() && !model.root());
	}));
	cl::Packet packet{};
	if (prepared) {
		packet = fixture.offered();
		fixture.send(packet, false);
	}
	if (explicitInvalidate)
		model.invalidate();
	else
		CHECK(storage.workspace.reset() == rt::Status::Ok);
	storage.release();
	CHECK(!model.nextPacket());
	CHECK(!model.prepareHandoff(18, prepared ? packet.requestId : 1));
	if (prepared)
		CHECK(model.onReply(cl::Input{fixture.reply}.first(123), packet.session, packet.requestId));
	model.process(fixture.now);
	CHECK(calls == 1 && model.pending() == 0);
	CHECK(!model.root() && !model.recoveryRequired());
	CHECK(device.reads == unsigned(prepared));
	model.close();
	model.process(fixture.now);
	CHECK(model.drained());
}

static void sameSchemaAndForeignModel() noexcept
{
	Encoded first;
	Encoded second;
	auto& model = first.inspector;
	CHECK(model.readField(first.bind(), [](const auto&) noexcept {
	}));
	first.pump();
	CHECK(second.inspector.readField(second.bind(), [](const auto&) noexcept {
	}));
	second.pump();
	const auto oldRoot = model.root().value;
	const auto oldLeaf = child(model, oldRoot, 7);
	CHECK(second.inspector.info(oldRoot).status == im::ModelStatus::Stale);
	CHECK(second.inspector.scalar(oldLeaf).status == im::ModelStatus::Stale);
	const auto before = model.snapshotInfo();
	CHECK(before.ready);
	unsigned refused = 0;
	const auto foreign = model.readField(second.bind(), [&](const auto&) noexcept {
		++refused;
	});
	CHECK(!foreign && foreign.status == cl::Status::NotAdmitted);
	CHECK(model.snapshotInfo().revision == before.revision);
	CHECK(model.info(oldRoot) && scalar<std::uint64_t>(model, oldLeaf) == UINT64_MAX);
	CHECK(refused == 0);
	unsigned calls = 0;
	CHECK(model.refresh([&](const auto& result) noexcept {
		++calls;
		CHECK(result.published);
	}));
	CHECK(model.snapshotInfo().revision > before.revision);
	CHECK(model.info(oldRoot).status == im::ModelStatus::Stale);
	CHECK(model.scalar(oldLeaf).status == im::ModelStatus::Stale);
	CHECK(model.root().status == im::ModelStatus::NoSnapshot);
	first.pump();
	CHECK(calls == 1 && model.root());
	CHECK(model.info(oldRoot).status == im::ModelStatus::Stale);
	CHECK(model.scalar(oldLeaf).status == im::ModelStatus::Stale);
	const auto freshRoot = model.root().value;
	CHECK(model.selectField(first.bind(4)) == cl::Status::Ok);
	CHECK(model.info(freshRoot).status == im::ModelStatus::Stale && !model.root());
	model.close();
	model.process(first.now);
	second.inspector.close();
	second.inspector.process(second.now);
	CHECK(model.drained() && second.inspector.drained());
}

static void olderCallbackAndReentrancy() noexcept
{
	Encoded fixture;
	auto& model = fixture.inspector;
	unsigned oldCalls = 0, newCalls = 0, refreshCalls = 0;
	const auto packetField = fixture.bind();
	const auto wideField = fixture.bind(4);
	CHECK(model.readField(packetField, [&](const auto& result) noexcept {
		++oldCalls;
		CHECK(result.operation.succeeded());
		CHECK(!result.published && result.modelStatus == im::ModelStatus::Stale);
		CHECK(!model.root());
	}));
	CHECK(model.selectField(wideField) == cl::Status::Ok);
	CHECK(model.readField(wideField, [&](const auto& result) noexcept {
		++newCalls;
		CHECK(result.published && model.root());
		const auto node = model.root().value;
		CHECK(scalar<std::uint64_t>(model, node) == UINT64_MAX);
		device.packet.u64 = 42;
		CHECK(model.refresh([&](const auto& next) noexcept {
			++refreshCalls;
			CHECK(next.published && model.root());
			CHECK(scalar<std::uint64_t>(model, model.root().value) == 42);
		}));
		CHECK(model.info(node).status == im::ModelStatus::Stale);
		CHECK(!model.root());
	}));
	fixture.pump();
	CHECK(oldCalls == 1 && newCalls == 1 && refreshCalls == 1);
	CHECK(model.snapshotInfo().fieldId == 4);
	CHECK(scalar<std::uint64_t>(model, model.root().value) == 42);
	model.close();
	model.process(fixture.now);
	CHECK(model.drained());
}

static void transientRxAndClosedCallback() noexcept
{
	Encoded fixture;
	auto& model = fixture.inspector;
	unsigned calls = 0;
	im::ReadResult copied;
	CHECK(model.readField(fixture.bind(), [&](const auto& result) noexcept {
		++calls;
		copied = result;
		CHECK(result.published);
	}));
	const auto packet = fixture.offered();
	fixture.send(packet, false);
	auto rx = std::make_unique<std::byte[]>(123);
	std::copy_n(fixture.reply.begin(), 123, rx.get());
	CHECK(model.onReply({rx.get(), 123}, packet.session, packet.requestId));
	rx.reset();
	model.process(fixture.now);
	CHECK(calls == 1 && copied.operation.succeeded() && copied.published);
	const auto node = child(model, model.root().value, 7);
	std::fill(fixture.rx.begin(), fixture.rx.end(), std::byte{0xcc});
	std::fill(fixture.reply.begin(), fixture.reply.end(), std::byte{0xdd});
	CHECK(scalar<std::uint64_t>(model, node) == UINT64_MAX);
	unsigned closed = 0;
	CHECK(model.readField(fixture.bind(), [&](const auto& result) noexcept {
		++closed;
		CHECK(result.operation.status == cl::Status::Cancelled);
		CHECK(result.modelStatus == im::ModelStatus::Stale && !result.published);
	}));
	model.close();
	CHECK(!model.drained());
	model.process(fixture.now);
	CHECK(closed == 1 && model.drained());
	CHECK(model.scalar(node).status == im::ModelStatus::Stale);
	CHECK(copied.operation.succeeded());
}

static void reparseSameWorkspace() noexcept
{
	Encoded fixture;
	auto& model = fixture.inspector;
	const auto oldField = fixture.bind();
	CHECK(model.readField(oldField, [](const auto& result) noexcept {
		CHECK(result.published);
	}));
	fixture.pump();
	const auto oldRoot = model.root().value;
	const auto oldLeaf = child(model, oldRoot, 7);
	const auto oldGeneration = fixture.metadata.schema.generation();
	CHECK(fixture.metadata.workspace.reset() == rt::Status::Ok);
	const auto parsed = rt::RuntimeSchema::parse(bytes, fixture.metadata.workspace);
	CHECK(parsed && parsed.schema.generation() > oldGeneration);
	CHECK(parsed.schema.findField("inspect", "Packet")->typeId == oldField.responseType());
	CHECK(model.info(oldRoot).status == im::ModelStatus::Stale);
	CHECK(model.scalar(oldLeaf).status == im::ModelStatus::Stale);
	CHECK(!model.isCurrent());
	model.process(fixture.now);
	CHECK(model.confirmQuiescence({17, true, true, true}));
	CHECK(model.admit(parsed.schema, cl::Input{bytes}.first(64), 18) == cl::Status::Ok);
	unsigned oldCalls = 0;
	const auto refused = model.readField(oldField, [&](const auto&) noexcept {
		++oldCalls;
	});
	CHECK(!refused && refused.status == cl::Status::NotAdmitted && oldCalls == 0);
	CHECK(model.readField(fixture.bind(), [](const auto& result) noexcept {
		CHECK(result.published);
	}));
	fixture.pump();
	CHECK(model.root());
	CHECK(model.info(oldRoot).status == im::ModelStatus::Stale);
	CHECK(model.scalar(oldLeaf).status == im::ModelStatus::Stale);
	CHECK(scalar<std::uint64_t>(model, child(model, model.root().value, 7)) == UINT64_MAX);
	model.close();
	model.process(fixture.now);
	CHECK(model.drained());
}

static void sessionDriftAndReentrantClose() noexcept
{
	Encoded fixture;
	auto& model = fixture.inspector;
	const auto field = fixture.bind();
	CHECK(model.readField(field, [](const auto& result) noexcept {
		CHECK(result.published);
	}));
	fixture.pump();
	const auto oldRoot = model.root().value;
	unsigned calls = 0;
	CHECK(model.readField(field, [&](const auto& result) noexcept {
		++calls;
		CHECK(result.operation.status == cl::Status::Cancelled);
		CHECK(result.valueStatus == dy::ValueStatus::Stale);
		CHECK(!result.published && result.modelStatus == im::ModelStatus::Stale);
	}));
	// Inject a port-side identity change while the queued read has no handoff.
	CHECK(fixture.backend.openSession(18));
	CHECK(!model.isCurrent() && !model.isCurrent(field));
	CHECK(model.info(oldRoot).status == im::ModelStatus::Stale);
	CHECK(!model.nextPacket());
	model.process(fixture.now);
	CHECK(calls == 1 && model.pending() == 0);
	CHECK(device.reads == 1);
	CHECK(model.confirmQuiescence({18, true, true, true}));
	CHECK(model.admit(fixture.metadata.schema, cl::Input{bytes}.first(64), 19) == cl::Status::Ok);
	CHECK(model.readField(fixture.bind(4), [&](const auto& result) noexcept {
		++calls;
		CHECK(result.published && result.operation.succeeded());
		const auto node = model.root().value;
		model.close();
		CHECK(model.info(node).status == im::ModelStatus::Stale);
		CHECK(!model.confirmQuiescence({19, true, true, true}));
	}));
	fixture.pump();
	model.process(fixture.now);
	CHECK(calls == 2 && model.drained());
	CHECK(!model.nextPacket() && !model.bindField(0));
}

int main()
{
	for (const bool explicitInvalidate : {false, true}) {
		releasedMetadata(explicitInvalidate);
		for (const bool prepared : {false, true})
			releasedPending(prepared, explicitInvalidate);
	}
	sameSchemaAndForeignModel();
	olderCallbackAndReentrancy();
	transientRxAndClosedCallback();
	reparseSameWorkspace();
	sessionDriftAndReentrantClose();
	CHECK(device.writes == 0 && device.commands == 0 && device.services == 0);
	std::printf("Inspector Lifetime: %u checks, 0 failures\n", checks);
	return 0;
}
