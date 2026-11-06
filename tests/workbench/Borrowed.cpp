/* Independent bounded Workbench views over the owner's real FIFO. MIT. */
#include "Fixture.hpp"
#include <integration/workbench/WorkbenchModel.hpp>
#include <concepts>
#include <bit>

using namespace workbench_fixture;
using View = wb::WorkbenchView<2, 512, 512, 256>;
using Fixture = Encoded<2, 512, 512, FieldData, 512>;

template<class T>
concept HasProcess = requires(T& value) { value.process(0); };
template<class T>
concept HasClose = requires(T& value) { value.close(); };
template<class T>
concept HasPacket = requires(T& value) { value.nextPacket(); };
template<class T>
concept HasHandoff = requires(T& value) { value.prepareHandoff(1, 1); };
template<class T>
concept HasReply = requires(T& value) { value.onReply(cl::Input{}, 1, 1); };
template<class T>
concept HasCancel = requires(T& value) { value.cancel(cl::RequestHandle{}); };
template<class T>
concept HasInvalidate = requires(T& value) { value.invalidate(); };
template<class T>
concept HasProof = requires(T& value) { value.confirmQuiescence(dy::Quiescence{}); };
template<class T>
concept HasAdmission =
    requires(T& value, const rt::RuntimeSchema& schema) { value.admit(schema, cl::Input{}, 1); };
static_assert(!HasProcess<View> && !HasClose<View> && !HasPacket<View> && !HasHandoff<View> &&
              !HasReply<View> && !HasCancel<View> && !HasInvalidate<View> && !HasProof<View> &&
              !HasAdmission<View>);
static_assert(!std::is_copy_constructible_v<View> && !std::is_move_constructible_v<View>);
static_assert(sizeof(View) < sizeof(wb::WorkbenchModel<2, 512, 512, 256>));

static auto field(View& view, std::uint32_t id = 0) noexcept
{
	const auto bound = view.bindField(id);
	CHECK(bound);
	return bound.endpoint;
}

static void independentViews() noexcept
{
	Fixture f;
	std::array<std::byte, 512> ar{}, br{};
	View a{f.inspector, ar}, b{f.inspector, br};
	CHECK(!a.isCurrent() && !b.isCurrent());
	CHECK(a.synchronize() == cl::Status::Ok && b.synchronize() == cl::Status::Ok);
	CHECK(a.session() == 17 && b.session() == 17 && f.inspector.pending() == 0);
	CHECK(!a.root() && !b.root() && !f.inspector.root());
	unsigned ac = 0, bc = 0;
	const auto first = a.readField(field(a), [&](const im::ReadResult& r) noexcept {
		++ac;
		CHECK(r.published && !a.drained());
	});
	const auto second = b.readField(field(b, 1), [&](const im::ReadResult& r) noexcept {
		++bc;
		CHECK(r.published && !b.drained());
	});
	CHECK(first && second && first.handle.owner == second.handle.owner &&
	      first.handle.sequence < second.handle.sequence);
	CHECK(f.inspector.pending() == 2 && !a.drained() && !b.drained());
	unsigned refused = 0;
	CHECK(a.readField(field(a), [&](const im::ReadResult&) noexcept {
		       ++refused;
	       }).status == cl::Status::QueueFull);
	f.pump();
	CHECK(ac == 1 && bc == 1 && refused == 0 && owner.reads == 2);
	CHECK(a.drained() && b.drained() && !f.inspector.root());
	const auto an = a.root(), bn = b.root();
	CHECK(an && bn && !a.info(bn.value) && !b.info(an.value));
	CHECK(scalar<std::uint64_t>(b, bn.value) == owner.packet.u64);
	CHECK(a.beginEdit() == rt::Status::Ok && !b.editing());
	CHECK(a.setScalar(child(a, an.value, 1), std::uint8_t{42}) == rt::Status::Ok);
	CHECK(b.root() && !b.canEdit());
	unsigned applied = 0;
	CHECK(a.apply([&](const wb::ApplyResult& r) noexcept {
		++applied;
		CHECK(r.write.succeeded() && r.refresh.published && !a.drained());
	}));
	b.invalidateData();
	CHECK(!b.root() && a.isCurrent() && b.isCurrent() && f.inspector.isCurrent());
	f.pump();
	CHECK(applied == 1 && owner.writes == 1 && owner.reads == 3 && owner.packet.u8 == 42);
	CHECK(a.root() && !b.root() && a.drained() && b.drained());
	CHECK(b.snapshotInfo().selected);
	CHECK(b.refresh([&](const im::ReadResult& r) noexcept {
		++bc;
		CHECK(r.published);
	}));
	f.pump();
	CHECK(bc == 2 && b.root() && owner.reads == 4);
	a.retire();
	b.retire();
	CHECK(a.synchronize() == cl::Status::Closed && b.synchronize() == cl::Status::Closed);
	CHECK(f.inspector.isCurrent());
}

static void retirementAndFreshness() noexcept
{
	Fixture f;
	View a{f.inspector}, b{f.inspector};
	CHECK(a.synchronize() == cl::Status::Ok && b.synchronize() == cl::Status::Ok);
	const auto command = a.bindCommand(0);
	CHECK(command && a.selectCommand(command.endpoint) == cl::Status::Ok);
	unsigned commands = 0, reads = 0;
	CHECK(a.execute([&](const im::OperationResult& r) noexcept {
		++commands;
		CHECK(r.executed() && !a.drained());
	}));
	CHECK(b.readField(field(b), [&](const im::ReadResult& r) noexcept {
		++reads;
		CHECK(r.operation.succeeded() && !r.published && r.valueStatus == dy::ValueStatus::Stale);
	}));
	a.retire();
	b.invalidateData();
	CHECK(!a.isCurrent() && !a.drained() && !b.drained());
	(void)f.send(f.offered());
	CHECK(commands == 1 && owner.commands == 1 && a.drained());
	CHECK(f.inspector.pending() == 1 && !b.drained());
	f.pump();
	CHECK(reads == 1 && !b.root() && b.drained() && owner.commands == 1);
	CHECK(b.selectField(field(b)) == cl::Status::Ok);
	CHECK(b.refresh([&](const im::ReadResult& r) noexcept {
		++reads;
		CHECK(r.published);
	}));
	f.pump();
	CHECK(reads == 2 && b.root());
	b.retire();
}

static void serviceStoresAndAdmission() noexcept
{
	Fixture f;
	std::array<std::byte, 512> ar{}, br{};
	View a{f.inspector, ar}, b{f.inspector, br};
	CHECK(a.synchronize() == cl::Status::Ok && b.synchronize() == cl::Status::Ok);
	const auto as = a.bindService(0), bs = b.bindService(0);
	CHECK(as && bs && a.selectService(as.endpoint) == cl::Status::Ok &&
	      b.selectService(bs.endpoint) == cl::Status::Ok);
	unsigned responses = 0;
	CHECK(a.callService([&](const wb::ServiceResult& r) noexcept {
		++responses;
		CHECK(r.operation.succeeded() && !r.published && r.valueStatus == dy::ValueStatus::Stale);
	}));
	CHECK(b.callService([&](const wb::ServiceResult& r) noexcept {
		++responses;
		CHECK(r.operation.succeeded() && r.published);
	}));
	a.invalidateData();
	f.pump();
	CHECK(responses == 2 && !a.serviceRoot() && b.serviceRoot() && owner.services == 2);
	const auto old = b.serviceRoot();
	f.inspector.invalidate();
	CHECK(!a.isCurrent() && !b.isCurrent() && !b.serviceInfo(old.value));
	f.inspector.process(f.now);
	CHECK(f.inspector.confirmQuiescence({17, true, true, true}));
	CHECK(f.inspector.admit(f.metadata.schema, FieldData::descriptor().first(64), 18) ==
	      cl::Status::Ok);
	CHECK(!b.isCurrent() && !b.serviceRoot());
	CHECK(b.synchronize() == cl::Status::Ok && b.session() == 18 && !b.serviceInfo(old.value));
	CHECK(!b.isCurrent(bs.endpoint));
	f.inspector.close();
	CHECK(b.bindField(0).status == cl::Status::Closed);
	a.retire();
	b.retire();
}

struct RetireOnMove {
	View* view;
	Fixture* fixture;
	unsigned* calls;

	RetireOnMove(View& v, Fixture& f, unsigned& n) noexcept : view(&v), fixture(&f), calls(&n)
	{}

	RetireOnMove(RetireOnMove&& other) noexcept
	    : view(other.view), fixture(other.fixture), calls(other.calls)
	{
		view->retire();
		fixture->inspector.process(fixture->now);
		CHECK(!fixture->inspector.nextPacket() && !fixture->inspector.drained());
		CHECK(fixture->inspector.bindField(0)); // read-only lookup remains available
		CHECK(!view->drained());
	}

	RetireOnMove(const RetireOnMove&) = delete;

	void operator()(const im::ReadResult& result) noexcept
	{
		++*calls;
		CHECK(!result.published && result.operation.status == cl::Status::Cancelled);
	}
};

static void publicationRetirement() noexcept
{
	Fixture f;
	View a{f.inspector};
	CHECK(a.synchronize() == cl::Status::Ok);
	const auto binding = field(a);
	unsigned calls = 0;
	const auto submitted = a.readField(binding, RetireOnMove{a, f, calls});
	CHECK(submitted && owner.reads == 0 && !a.drained());
	f.inspector.process(f.now);
	CHECK(calls == 1 && owner.reads == 0 && a.drained());
	CHECK(f.inspector.isCurrent());
}

static void existingOwnerResult() noexcept
{
	Fixture f;
	unsigned callbacks = 0;
	CHECK(f.inspector.readField(f.bind(), [&](const im::ReadResult& result) noexcept {
		++callbacks;
		CHECK(result.operation.succeeded() && !result.published &&
		      result.modelStatus == im::ModelStatus::Stale);
		CHECK(result.valueStatus == dy::ValueStatus::Ok); // Runtime validity remains independent.
	}));
	CHECK(f.inspector.selectField(f.bind(1)) == cl::Status::Ok);
	f.pump();
	CHECK(callbacks == 1 && !f.inspector.root());
}

static void clockReentry() noexcept
{
	using W = wb::WorkbenchModel<2, 512, 512, 256>;
	for (unsigned action = 0; action != 3; ++action) {
		for (const bool retire : {false, true}) {
			Metadata metadata;
			owner = {};

			struct Hook {
				View* view = nullptr;
				W* model = nullptr;
				bool armed = false, retire = false;
			} hook;

			cl::Clock clock{&hook, +[](void* pointer) noexcept {
				                auto& value = *static_cast<Hook*>(pointer);
				                if (value.armed) {
					                value.armed = false;
					                if (value.retire)
						                value.view->retire();
					                else
						                value.view->invalidateData();
					                value.model->process(0);
					                CHECK(!value.model->nextPacket() && !value.model->drained());
				                }
				                return std::uint32_t{0};
			                }};
			std::array<std::byte, 1024> tx{}, rx{}, reply{}, scratch{};
			std::array<std::byte, 512> response{};
			telemetry::Workspace workspace{scratch};
			en::EncodedBackend backend{tx, rx, clock};
			W model{backend, clock};
			CHECK(model.admit(metadata.schema, FieldData::descriptor().first(64), 17) ==
			      cl::Status::Ok);
			View view{model, response}, peer{model};
			hook.view = &view;
			hook.model = &model;
			hook.retire = retire;
			CHECK(view.synchronize() == cl::Status::Ok && peer.synchronize() == cl::Status::Ok);
			const auto pump = [&]() noexcept {
				for (unsigned i = 0; i != 8 && model.pending(); ++i) {
					model.process(0);
					const auto packet = model.nextPacket();
					if (!packet)
						continue;
					CHECK(model.prepareHandoff(packet.session, packet.requestId));
					CHECK(model.onSendAccepted(packet.session, packet.requestId));
					const auto result = en::dispatchEncoded(FieldData::model(), workspace,
					                                        packet.session, packet.bytes, reply);
					CHECK(result);
					CHECK(model.onReply(cl::Input{reply}.first(result.written), packet.session,
					                    packet.requestId));
				}
				model.process(0);
				CHECK(model.pending() == 0);
			};
			if (action == 0) {
				CHECK(view.readField(field(view), [](const im::ReadResult& r) noexcept {
					CHECK(r.published);
				}));
				pump();
				CHECK(view.beginEdit() == rt::Status::Ok);
				CHECK(view.setScalar(child(view, view.root().value, 1), std::uint8_t{77}) ==
				      rt::Status::Ok);
			} else if (action == 1) {
				const auto binding = view.bindCommand(0);
				CHECK(binding && view.selectCommand(binding.endpoint) == cl::Status::Ok);
			} else {
				const auto binding = view.bindService(0);
				CHECK(binding && view.selectService(binding.endpoint) == cl::Status::Ok);
			}
			unsigned callbacks = 0;
			hook.armed = true;
			const auto submitted = [&]() noexcept {
				if (action == 0)
					return view.apply([&](const wb::ApplyResult& r) noexcept {
						++callbacks;
						CHECK(r.write.status == cl::Status::Cancelled && !r.refreshAttempted);
					});
				if (action == 1)
					return view.execute([&](const im::OperationResult& r) noexcept {
						++callbacks;
						CHECK(r.status == cl::Status::Cancelled);
					});
				return view.callService([&](const wb::ServiceResult& r) noexcept {
					++callbacks;
					CHECK(r.operation.status == cl::Status::Cancelled && !r.published);
				});
			}();
			CHECK(submitted && !hook.armed && !view.drained() && callbacks == 0);
			pump();
			CHECK(callbacks == 1 && view.drained() && model.isCurrent() && peer.isCurrent());
			CHECK(owner.writes == 0 && owner.commands == 0 && owner.services == 0);
			CHECK(!view.root() && !view.serviceRoot() && !view.canExecute() && !view.canApply());
			CHECK(peer.readField(field(peer), [](const im::ReadResult& r) noexcept {
				CHECK(r.published);
			}));
			pump();
			CHECK(peer.root());
			view.retire();
			peer.retire();
			model.close();
			model.process(0);
			CHECK(view.drained() && peer.drained() && model.drained());
		}
	}
}

template<class F, std::size_t N>
static void exactRoundTrips() noexcept
{
	using V = wb::WorkbenchView<N, 512, 512, 256>;
	constexpr std::array<std::uint32_t, 6> floats{0x7fc12345U, 0xffc23456U, 0x7f812345U,
	                                              0xff823456U, 0U,          0x80000000U};
	constexpr std::array<std::uint64_t, 6> doubles{0x7ff8123456789abcULL,
	                                               0xfff823456789abcdULL,
	                                               0x7ff0123456789abcULL,
	                                               0xfff023456789abcdULL,
	                                               0ULL,
	                                               0x8000000000000000ULL};
	for (std::size_t i = 0; i != floats.size(); ++i) {
		F f;
		std::array<std::byte, 512> response{};
		V a{f.inspector}, b{f.inspector, response};
		CHECK(a.synchronize() == cl::Status::Ok && b.synchronize() == cl::Status::Ok);
		owner.packet.f32 = std::bit_cast<float>(floats[i]);
		owner.packet.f64 = std::bit_cast<double>(doubles[i]);
		const auto binding = a.bindField(0);
		CHECK(binding);
		unsigned reads = 0;
		CHECK(a.readField(binding.endpoint, [&](const im::ReadResult& result) noexcept {
			++reads;
			CHECK(result.published);
		}));
		f.pump();
		CHECK(reads == 1 && a.root());
		const auto root = a.root().value;
		CHECK(a.beginEdit() == rt::Status::Ok);
		CHECK(a.setScalar(child(a, root, 1), std::uint8_t{67}) == rt::Status::Ok);
		unsigned writes = 0;
		CHECK(a.apply([&](const wb::ApplyResult& result) noexcept {
			++writes;
			CHECK(result.write.succeeded() && result.refresh.published);
		}));
		f.pump();
		CHECK(writes == 1 && owner.writes == 1 && owner.reads == 2 && owner.packet.u8 == 67);
		CHECK(std::bit_cast<std::uint32_t>(owner.packet.f32) == floats[i]);
		CHECK(std::bit_cast<std::uint64_t>(owner.packet.f64) == doubles[i]);
		const auto bound = b.bindService(0);
		CHECK(bound && b.selectService(bound.endpoint) == cl::Status::Ok);
		unsigned services = 0;
		CHECK(b.callService([&](const wb::ServiceResult& result) noexcept {
			++services;
			CHECK(result.operation.succeeded() && result.published);
		}));
		f.pump();
		CHECK(services == 1 && b.serviceRoot() && a.root());
		const auto serviceRoot = b.serviceRoot().value;
		const auto fv = b.serviceScalar(b.serviceChild(serviceRoot, 9).value);
		const auto dv = b.serviceScalar(b.serviceChild(serviceRoot, 10).value);
		CHECK(fv && dv);
		CHECK(std::bit_cast<std::uint32_t>(*std::get_if<float>(&fv.value.value)) == floats[i]);
		CHECK(std::bit_cast<std::uint64_t>(*std::get_if<double>(&dv.value.value)) == doubles[i]);
		// Responses and field snapshots have independent identities and owned bytes.
		CHECK(!a.info(serviceRoot));
		a.retire();
		b.retire();
		CHECK(a.drained() && b.drained());
	}
}

int main()
{
	independentViews();
	retirementAndFreshness();
	serviceStoresAndAdmission();
	publicationRetirement();
	existingOwnerResult();
	clockReentry();
	exactRoundTrips<Fixture, 2>();
	exactRoundTrips<RpcModel<FieldData, 512>, 4>();
	std::printf("Workbench Borrowed: %u checks, 0 failures\n", inspector_fixture::checks);
	return 0;
}
