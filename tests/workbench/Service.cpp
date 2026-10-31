/* Real Service request/owned-response acceptance over one Encoded/RPC FIFO. MIT. */
#include "ServiceFixture.hpp"
#include <bit>
#include <new>
namespace svc = service_fixture;
namespace cl = telemetry::client;
namespace rt = telemetry::runtime;
namespace im = telemetry::integration::inspector;
namespace wb = telemetry::integration::workbench;
namespace dy = telemetry::integration::dynamic;

static void succeeded(const wb::ServiceResult& r) noexcept
{
	CHECK(r.operation.succeeded() && r.operation.operation == cl::Operation::Service);
	CHECK(!r.operation.accepted() && !r.operation.executed());
	CHECK(r.valueStatus == dy::ValueStatus::Ok && r.modelStatus == im::ModelStatus::Ok &&
	      r.published);
}

template<class Fixture>
static void exactResponse() noexcept
{
	Fixture f;
	auto& m = f.inspector;
	const auto field = workbench_fixture::read(f);
	const auto fieldLeaf = workbench_fixture::child(m, field, 7);
	const auto cached = m.snapshotInfo();
	CHECK(m.services().size() == 6);
	const auto binding = svc::select(f);
	CHECK(m.snapshotInfo().ready && m.info(fieldLeaf));
	const auto root = svc::root(m);
	CHECK(m.editorInfo(root).value.name == "Transform" &&
	      m.editorSnapshotInfo().type == binding.requestType());
	CHECK(m.canCallService() && !m.canExecute() && !m.canApply() && !m.canEdit());
	const auto u64 = svc::child(m, root, 7), i64 = svc::child(m, root, 8);
	CHECK(svc::scalar<std::uint64_t>(m, u64) == 0);
	CHECK(m.setScalar(u64, std::uint32_t{12}) == rt::Status::TypeMismatch);
	CHECK(m.setScalar(u64, UINT64_MAX) == rt::Status::Ok);
	CHECK(m.setScalar(i64, INT64_MIN) == rt::Status::Ok);
	const auto f32 = svc::child(m, root, 9), f64 = svc::child(m, root, 10);
	CHECK(m.setScalar(f32, std::bit_cast<float>(std::uint32_t{0xff812345})) == rt::Status::Ok);
	CHECK(m.setScalar(f64, std::bit_cast<double>(std::uint64_t{0xfff0000000001234ULL})) ==
	      rt::Status::Ok);
	CHECK(m.setScalar(svc::child(m, svc::child(m, root, 11), 2), std::int16_t{-123}) ==
	      rt::Status::Ok);
	const auto matrix = svc::child(m, svc::child(m, svc::child(m, root, 13), 1), 0);
	CHECK(m.setScalar(matrix, std::uint32_t{31337}) == rt::Status::Ok);
	unsigned callbacks = 0;
	wb::ServiceResult saved;
	CHECK(m.callService([&](const wb::ServiceResult& r) noexcept {
		++callbacks;
		succeeded(r);
		saved = r;
		CHECK(r.serviceId == 0 && m.serviceSnapshotInfo().revision == r.revision);
		const auto response = svc::responseRoot(m);
		CHECK(m.serviceInfo(response).value.name == "Transform");
		CHECK(svc::scalar<std::uint64_t>(m, svc::responseChild(m, response, 7), true) ==
		      UINT64_MAX);
		CHECK(m.callService([](const wb::ServiceResult&) noexcept {
		       }).status == cl::Status::Busy);
		CHECK(m.setScalar(u64, std::uint64_t{24}) == rt::Status::Ok);
	}));
	CHECK(m.phase() == wb::Phase::Executing && !m.canCallService());
	CHECK(!m.root() && !m.info(fieldLeaf) && m.snapshotInfo().revision > cached.revision);
	CHECK(m.setScalar(u64, std::uint64_t{9}) == rt::Status::Stale);
	CHECK(m.selectService(binding) == cl::Status::Busy);
	f.pump();
	CHECK(callbacks == 1 && svc::device.calls == 1 && svc::device.reads == 1);
	CHECK(m.canCallService() && m.editing() && saved.published);
	const auto response = svc::responseRoot(m);
	CHECK(m.serviceChildCount(response).value == 17 && !m.info(response) && !m.serviceInfo(root));
	const auto ru64 = svc::responseChild(m, response, 7), ri64 = svc::responseChild(m, response, 8);
	CHECK(svc::scalar<std::uint64_t>(m, ru64, true) == UINT64_MAX);
	CHECK(svc::scalar<std::int64_t>(m, ri64, true) == INT64_MIN);
	std::array<char, 32> text{};
	const auto decimal = m.serviceDecimal(ru64, text);
	CHECK(decimal);
	CHECK(std::string_view(text.data(), decimal.value) == "18446744073709551615");
	CHECK(std::bit_cast<std::uint32_t>(
	          svc::scalar<float>(m, svc::responseChild(m, response, 9), true)) == 0xff812345);
	CHECK(std::bit_cast<std::uint64_t>(svc::scalar<double>(m, svc::responseChild(m, response, 10),
	                                                       true)) == 0xfff0000000001234ULL);
	const auto enumValue =
	    m.serviceScalar(svc::responseChild(m, svc::responseChild(m, response, 11), 2));
	CHECK(enumValue && enumValue.value.enumValue && !enumValue.value.enumKnown &&
	      std::get<std::int16_t>(enumValue.value.value) == -123);
	CHECK(svc::device.last.matrix[1][0] == 31337 && svc::device.last.u64 == UINT64_MAX);
	CHECK(svc::scalar<std::uint64_t>(m, u64) == 24);
	// Overwrite every driver/server temporary after callback; the response is
	// owned independently of RX, native state, and the editable arguments.
	std::fill(f.reply.begin(), f.reply.end(), std::byte{0xa5});
	std::fill(f.port.begin(), f.port.end(), std::byte{0xa5});
	std::fill(f.scratch.begin(), f.scratch.end(), std::byte{0xa5});
	if constexpr (requires { f.rx; })
		std::fill(f.rx.begin(), f.rx.end(), std::byte{0xa5});
	if constexpr (requires { f.result; })
		std::fill(f.result.begin(), f.result.end(), std::byte{0xa5});
	svc::device.packet = {};
	svc::device.last = {};
	CHECK(svc::scalar<std::uint64_t>(m, ru64, true) == UINT64_MAX);
	CHECK(std::bit_cast<std::uint64_t>(svc::scalar<double>(m, svc::responseChild(m, response, 10),
	                                                       true)) == 0xfff0000000001234ULL);
	CHECK(m.callService([&](const wb::ServiceResult& r) noexcept {
		++callbacks;
		succeeded(r);
	}));
	CHECK(!m.serviceInfo(response) && !m.serviceSnapshotInfo().ready);
	f.pump();
	CHECK(callbacks == 2 && svc::device.calls == 2 && svc::device.last.u64 == 24);
	CHECK(saved.published && saved.serviceId == 0); // owned outcome remains historical
	svc::device.refuse = true;
	CHECK(m.callService([&](const wb::ServiceResult& r) noexcept {
		++callbacks;
		CHECK(r.operation.status == cl::Status::Ok && r.operation.dispatch == 0 &&
		      r.operation.endpointStatus == 1);
		CHECK(!r.operation.succeeded() && !r.operation.accepted() && !r.published);
		CHECK(r.valueStatus == dy::ValueStatus::NoValue && r.modelStatus == im::ModelStatus::Ok);
	}));
	f.pump();
	CHECK(callbacks == 3 && svc::device.calls == 3 && !m.serviceRoot() && m.canCallService());
}

template<class Fixture>
static void voidEmptyAndRead() noexcept
{
	Fixture f;
	auto& m = f.inspector;
	(void)svc::select(f, 2);
	CHECK(m.editorSnapshotInfo().type == 0 && !m.editorRoot());
	unsigned callbacks = 0;
	CHECK(m.callService([&](const wb::ServiceResult& r) noexcept {
		++callbacks;
		CHECK(r.operation.succeeded() && !r.published && r.valueStatus == dy::ValueStatus::NoValue);
		CHECK(r.modelStatus == im::ModelStatus::Ok && !m.serviceRoot());
	}));
	f.pump();
	CHECK(callbacks == 1 && m.canCallService());
	(void)svc::select(f, 3);
	const auto request = svc::root(m);
	CHECK(m.editorSnapshotInfo().type != 0 && m.editorSnapshotInfo().bytes == 0 &&
	      m.editorChildCount(request).value == 0);
	CHECK(m.callService([&](const wb::ServiceResult& r) noexcept {
		++callbacks;
		succeeded(r);
	}));
	f.pump();
	const auto response = svc::responseRoot(m);
	CHECK(m.serviceSnapshotInfo().type != 0 && m.serviceSnapshotInfo().bytes == 0 &&
	      m.serviceChildCount(response).value == 0);
	(void)svc::select(f, 1);
	CHECK(!m.editorRoot() && m.canCallService());
	CHECK(m.serviceInfo(response)); // mere selection preserves last response
	CHECK(m.callService([&](const wb::ServiceResult& r) noexcept {
		++callbacks;
		succeeded(r);
	}));
	f.pump();
	CHECK(callbacks == 3 && svc::device.calls == 3);
}

static void capacityAndMemory() noexcept
{
	{
		svc::Encoded<4, 0> f;
		auto& m = f.inspector;
		const auto binding = svc::select(f);
		const auto request = svc::root(m);
		CHECK(!m.canCallService());
		const auto refused = m.callService([](const wb::ServiceResult&) noexcept {
		});
		CHECK(refused.status == cl::Status::BufferTooSmall &&
		      refused.modelStatus == im::ModelStatus::SnapshotTooLarge);
		CHECK(m.pending() == 0 && !m.nextPacket() && svc::device.calls == 0 &&
		      m.editorInfo(request));
		const auto wrong = m.bindCommand(0);
		CHECK(wrong);
		CHECK(m.selectService(wrong.endpoint) == cl::Status::InvalidArgument &&
		      m.isCurrent(binding));
		void (*null)(const wb::ServiceResult&) noexcept = nullptr;
		CHECK(m.callService(null).status == cl::Status::InvalidArgument);
		for (const auto t : {std::uint32_t{0}, cl::maxTimeoutMs + 1})
			CHECK(m.callService(
			           [](const wb::ServiceResult&) noexcept {
			           },
			           t)
			          .status == cl::Status::InvalidArgument);
	}
}

static void noStorageVoidEmpty() noexcept
{
	svc::Encoded<4, 0> f;
	unsigned callbacks = 0;
	for (const auto id : {2U, 3U}) {
		(void)svc::select(f, id);
		CHECK(f.inspector.canCallService());
		CHECK(f.inspector.callService([&](const wb::ServiceResult& r) noexcept {
			++callbacks;
			CHECK(r.operation.succeeded());
			CHECK(r.published == (id == 3));
		}));
		f.pump();
	}
	CHECK(callbacks == 2 && svc::device.calls == 2);
	(void)svc::select(f, 4);
	CHECK(!f.inspector.canCallService());
	CHECK(f.inspector
	          .callService([](const wb::ServiceResult&) noexcept {
	          })
	          .status == cl::Status::BufferTooSmall);
}

static void overlap() noexcept
{
	using Model = wb::WorkbenchModel<2, 512, 512, 256>;
	svc::Encoded<> f;
	std::array<std::byte, 1024> tx{}, rx{};
	svc::en::EncodedBackend backend{tx, rx, f.clock};
	// A caller cannot use either model control bytes or live schema indexes as
	// response payload storage, even for a Service whose response is Void.
	alignas(Model) std::array<std::byte, sizeof(Model)> memory{};
	auto* alias = std::construct_at(reinterpret_cast<Model*>(memory.data()), backend, f.clock,
	                                std::span{memory}.first(16));
	const auto before = memory;
	CHECK(alias->admit(f.metadata.schema, cl::Input{svc::descriptorBytes}.first(64), 17) ==
	      cl::Status::InvalidArgument);
	CHECK(memory == before);
	alias->close();
	alias->process(0);
	CHECK(alias->drained());
	std::destroy_at(alias);
	const auto rows = std::as_writable_bytes(std::span{f.metadata.types});
	std::array<std::byte, sizeof(f.metadata.types)> copy{};
	std::copy(rows.begin(), rows.end(), copy.begin());
	Model metadataAlias{backend, f.clock, rows};
	CHECK(metadataAlias.admit(f.metadata.schema, cl::Input{svc::descriptorBytes}.first(64), 17) ==
	      cl::Status::InvalidArgument);
	CHECK(std::equal(rows.begin(), rows.end(), copy.begin()));
	CHECK(metadataAlias.admit(rt::RuntimeSchema{}, cl::Input{svc::descriptorBytes}.first(64), 17) ==
	      cl::Status::NotAdmitted);
	metadataAlias.close();
	metadataAlias.process(0);
	CHECK(metadataAlias.drained());
}

static void queueAndSnapshot() noexcept
{
	{
		svc::Encoded<1> f;
		auto& m = f.inspector;
		(void)svc::select(f, 1);
		CHECK(m.callService([](const wb::ServiceResult& r) noexcept {
			succeeded(r);
		}));
		f.pump();
		const auto previous = svc::responseRoot(m);
		unsigned refusedCallbacks = 0;
		CHECK(m.readField(f.bind(), [&](const im::ReadResult& r) noexcept {
			CHECK(r.published);
			(void)svc::select(f);
			const auto leaf = svc::child(m, svc::root(m), 7);
			CHECK(m.setScalar(leaf, std::uint64_t{123}) == rt::Status::Ok);
			const auto refused = m.callService([&](const wb::ServiceResult&) noexcept {
				++refusedCallbacks;
			});
			CHECK(refused.status == cl::Status::QueueFull && m.canCallService());
			CHECK(m.snapshotInfo().ready && m.serviceInfo(previous));
			CHECK(svc::scalar<std::uint64_t>(m, leaf) == 123);
		}));
		f.pump();
		CHECK(refusedCallbacks == 0 && svc::device.calls == 1);
		CHECK(m.callService([](const wb::ServiceResult& r) noexcept {
			succeeded(r);
		}));
		CHECK(!m.serviceInfo(previous) && !m.snapshotInfo().ready);
		f.pump();
		CHECK(svc::device.calls == 2 && svc::device.last.u64 == 123);
	}
	{
		svc::Encoded<> f;
		auto& m = f.inspector;
		unsigned reads = 0, calls = 0;
		CHECK(m.readField(f.bind(), [&](const im::ReadResult& r) noexcept {
			++reads;
			CHECK(r.operation.succeeded() && !r.published);
		}));
		(void)svc::select(f);
		CHECK(m.callService([&](const wb::ServiceResult& r) noexcept {
			++calls;
			succeeded(r);
		}));
		f.pump();
		CHECK(reads == 1 && calls == 1 && svc::device.calls == 1 && svc::device.reads == 1 &&
		      !m.snapshotInfo().ready);
	}
}

static void phaseBoundaries() noexcept
{
	for (unsigned ending = 0; ending != 4; ++ending)
		for (unsigned phase = 0; phase != 3; ++phase) {
			svc::Encoded<> f;
			auto& m = f.inspector;
			(void)svc::select(f);
			unsigned callbacks = 0;
			cl::Status status = cl::Status::Ok;
			const auto submitted = m.callService(
			    [&](const wb::ServiceResult& r) noexcept {
				    ++callbacks;
				    status = r.operation.status;
				    CHECK(!r.published);
			    },
			    20);
			CHECK(submitted);
			cl::Packet packet{};
			std::size_t replyBytes = 0;
			if (phase)
				packet = f.offered();
			if (phase == 2)
				replyBytes = f.send(packet, false);
			if (ending == 0)
				f.now = 20;
			if (ending == 1)
				CHECK(m.cancel(submitted.handle));
			if (ending == 2)
				m.invalidate();
			if (ending == 3)
				m.close();
			m.process(f.now);
			if (ending == 2 && phase == 2) {
				CHECK(callbacks == 0 && !m.nextPacket());
				f.now = 20;
				m.process(f.now);
			}
			CHECK(callbacks == 1 && m.pending() == 0);
			CHECK(status == (phase == 2    ? cl::Status::OutcomeUnknown
			                 : ending == 0 ? cl::Status::Timeout
			                               : cl::Status::Cancelled));
			CHECK(m.recoveryRequired() == (phase == 2) &&
			      svc::device.calls == (phase == 2 ? 1U : 0U));
			CHECK(!m.serviceRoot());
			if (phase == 2) {
				CHECK(!m.serviceSelected() && !m.canCallService());
				CHECK(!m.onReply(cl::Input{f.reply}.first(replyBytes), packet.session,
				                 packet.requestId));
			}
			m.process(100);
			CHECK(callbacks == 1 && svc::device.calls == (phase == 2 ? 1U : 0U));
			if (ending == 0)
				std::printf(
				    "Workbench Service timeout phase=%s callbacks=%u calls=%u recovery=%u\n",
				    phase == 0   ? "queued"
				    : phase == 1 ? "offered"
				                 : "prepared",
				    callbacks, svc::device.calls, unsigned(m.recoveryRequired()));
		}
}

static void staleAndPassive() noexcept
{
	for (const bool afterDeadline : {false, true}) {
		svc::Encoded<> f;
		auto& m = f.inspector;
		(void)svc::select(f);
		const auto request = svc::root(m);
		unsigned callbacks = 0;
		CHECK(m.callService(
		    [&](const wb::ServiceResult& r) noexcept {
			    ++callbacks;
			    CHECK(r.operation.status ==
			          (afterDeadline ? cl::Status::OutcomeUnknown : cl::Status::Ok));
			    CHECK(!r.published && r.valueStatus == dy::ValueStatus::Stale &&
			          r.modelStatus == im::ModelStatus::Stale);
		    },
		    20));
		const auto packet = f.offered();
		const auto size = f.send(packet, false);
		CHECK(f.metadata.workspace.reset() == rt::Status::Ok);
		CHECK(!m.editorInfo(request) && !m.serviceRoot() && !m.nextPacket());
		if (afterDeadline)
			f.now = 20;
		const auto accepted =
		    m.onReply(cl::Input{f.reply}.first(size), packet.session, packet.requestId);
		CHECK(accepted == !afterDeadline);
		m.process(f.now);
		CHECK(callbacks == 1 && svc::device.calls == 1 && !m.canCallService());
	}
	{
		svc::Encoded<> f;
		(void)svc::select(f, 5);
		unsigned callbacks = 0;
		CHECK(f.inspector.callService([&](const wb::ServiceResult& r) noexcept {
			++callbacks;
			CHECK(r.operation.status == cl::Status::Ok && !r.published &&
			      r.valueStatus == dy::ValueStatus::InvalidPayload);
		}));
		const auto p = f.offered();
		const auto n = f.send(p, false);
		CHECK(n > 0);
		f.reply[n - 1] = std::byte{2};
		CHECK(f.inspector.onReply(cl::Input{f.reply}.first(n), p.session, p.requestId));
		f.inspector.process(0);
		CHECK(callbacks == 1 && !f.inspector.serviceRoot());
	}
}

static void normalReentrancy() noexcept
{
	svc::Encoded<2> f;
	auto& m = f.inspector;
	(void)svc::select(f);
	const auto arg = svc::child(m, svc::root(m), 7);
	CHECK(m.setScalar(arg, std::uint64_t{75}) == rt::Status::Ok);
	unsigned calls = 0, reads = 0;
	CHECK(m.callService([&](const wb::ServiceResult& r) noexcept {
		++calls;
		succeeded(r);
		CHECK(m.setScalar(arg, std::uint64_t{99}) == rt::Status::Ok);
		const auto response = svc::responseRoot(m);
		CHECK(m.readField(f.bind(), [&](const im::ReadResult& read) noexcept {
			++reads;
			CHECK(read.published);
		}));
		CHECK(!m.serviceSelected() && m.serviceInfo(response) && r.published);
		CHECK(m.callService([](const wb::ServiceResult&) noexcept {
		       }).status == cl::Status::Busy);
	}));
	f.pump();
	CHECK(calls == 1 && reads == 1 && svc::device.calls == 1 && svc::device.reads == 1 &&
	      svc::device.last.u64 == 75);
}

template<class Fixture>
static void largeResponse() noexcept
{
	Fixture f;
	(void)svc::select(f, 4);
	CHECK(f.inspector.canCallService());
	unsigned callbacks = 0;
	CHECK(f.inspector.callService([&](const wb::ServiceResult& r) noexcept {
		++callbacks;
		succeeded(r);
	}));
	f.pump();
	const auto root = svc::responseRoot(f.inspector);
	const auto array = svc::responseChild(f.inspector, root, 0);
	CHECK(f.inspector.serviceChildCount(array).value == 700);
	CHECK(svc::scalar<std::uint8_t>(f.inspector, svc::responseChild(f.inspector, array, 699),
	                                true) == 0);
	CHECK(f.inspector.serviceSnapshotInfo().bytes == 700 && callbacks == 1 &&
	      svc::device.calls == 1);
	CHECK(!f.inspector.serviceChild(array, 700));
	f.inspector.invalidate();
	CHECK(!f.inspector.serviceInfo(root) && !f.inspector.serviceScalar(array));
	CHECK(f.metadata.workspace.reset() == rt::Status::Ok);
	std::fill(f.responseStorage.begin(), f.responseStorage.end(), std::byte{0xa5});
	CHECK(!f.inspector.serviceChild(root, 0) && !f.inspector.serviceRoot());
}

static void rpcBoundary() noexcept
{
	for (const bool finalAuthorization : {false, true}) {
		svc::Rpc f;
		(void)svc::select(f);
		unsigned callbacks = 0;
		cl::Status status = cl::Status::Ok;
		CHECK(f.inspector.callService(
		    [&](const wb::ServiceResult& r) noexcept {
			    ++callbacks;
			    status = r.operation.status;
			    CHECK(!r.published);
		    },
		    100));
		for (unsigned i = 0; i != 40; ++i) {
			const auto packet = f.inspector.nextPacket();
			if (!packet) {
				(void)f.pool.process(++f.now);
				f.inspector.process(f.now);
				continue;
			}
			if (packet.mayExecute && finalAuthorization) {
				CHECK(f.inspector.prepareHandoff(packet.session, packet.requestId));
				break;
			}
			CHECK(!packet.mayExecute);
			std::copy(packet.bytes.begin(), packet.bytes.end(), f.port.begin());
			CHECK(f.inspector.prepareHandoff(packet.session, packet.requestId));
			CHECK(f.inspector.onSendAccepted(packet.session, packet.requestId));
			const auto response =
			    f.pool.handle(f.session, cl::Input{f.port}.first(packet.bytes.size()), f.reply);
			CHECK(response.status == svc::rpc::RpcSessionStatus::Ok &&
			      response.reply.status == resource::Status::Ok);
			if (response.ticket.serial)
				CHECK(f.pool.replyAccepted(f.session, response.ticket) ==
				      svc::rpc::RpcSessionStatus::Ok);
			CHECK(f.inspector.onReply(cl::Input{f.reply}.first(response.reply.written),
			                          packet.session, packet.requestId));
			if (!finalAuthorization)
				break;
		}
		CHECK(svc::device.calls == 0 &&
		      f.mailbox.state() == telemetry::protocol::rpc::RpcState::Receiving);
		f.inspector.invalidate();
		CHECK(!f.inspector.nextPacket());
		f.inspector.process(f.now);
		if (finalAuthorization) {
			CHECK(callbacks == 0);
			f.now = 100;
			f.inspector.process(f.now);
		}
		CHECK(callbacks == 1 &&
		      status == (finalAuthorization ? cl::Status::OutcomeUnknown : cl::Status::Cancelled));
		CHECK(!f.inspector.confirmQuiescence({1, false, true, true}));
		CHECK(f.pool.release(f.session) == svc::rpc::RpcSessionStatus::Ok);
		(void)f.pool.process(f.now);
		CHECK(f.mailbox.state() == telemetry::protocol::rpc::RpcState::Idle);
		CHECK(f.inspector.confirmQuiescence({1, true, true, true}) && svc::device.calls == 0);
	}
	// Handoff authorization already marks uncertainty, even if the port never
	// acknowledges or physically delivers any bytes to the native endpoint.
	svc::Encoded<> f;
	(void)svc::select(f);
	unsigned callbacks = 0;
	CHECK(f.inspector.callService(
	    [&](const wb::ServiceResult& r) noexcept {
		    ++callbacks;
		    CHECK(r.operation.status == cl::Status::OutcomeUnknown && !r.published);
	    },
	    20));
	const auto packet = f.offered();
	CHECK(f.inspector.prepareHandoff(packet.session, packet.requestId));
	f.now = 20;
	f.inspector.process(f.now);
	CHECK(callbacks == 1 && svc::device.calls == 0 && f.inspector.recoveryRequired());
}

struct OwnershipState {
	void* context;
	void (*moving)(void*) noexcept = nullptr;
	void (*destroying)(void*) noexcept = nullptr;
	unsigned moves = 0, destructions = 0, callbacks = 0;
};

struct OwnershipProbe {
	OwnershipState* state;

	explicit OwnershipProbe(OwnershipState& s) noexcept : state(&s)
	{}

	OwnershipProbe(const OwnershipProbe&) = delete;

	OwnershipProbe(OwnershipProbe&& other) noexcept : state(std::exchange(other.state, nullptr))
	{
		if (state && state->moves++ == 0 && state->moving)
			state->moving(state->context);
	}

	~OwnershipProbe()
	{
		if (state) {
			++state->destructions;
			if (state->destroying)
				state->destroying(state->context);
		}
	}

	void operator()(const wb::ServiceResult& r) noexcept
	{
		++state->callbacks;
		succeeded(r);
	}
};

static void ownershipPublication() noexcept
{
	{
		svc::Encoded<> f;
		auto& m = f.inspector;
		unsigned reads = 0;
		CHECK(m.readField(f.bind(), [&](const im::ReadResult&) noexcept {
			++reads;
		}));
		(void)svc::select(f);
		const auto arg = svc::child(m, svc::root(m), 7);
		CHECK(m.setScalar(arg, std::uint64_t{73}) == rt::Status::Ok);

		struct Context {
			svc::Encoded<>* fixture;
			im::NodeHandle arg;
			unsigned* reads;
		} context{&f, arg, &reads};

		OwnershipState state{
		    &context,
		    +[](void* p) noexcept {
			    auto& c = *static_cast<Context*>(p);
			    auto& m = c.fixture->inspector;
			    CHECK(m.selectService(m.bindService(2).endpoint) == cl::Status::Busy);
			    CHECK(m.selectCommand(m.bindCommand(0).endpoint) == cl::Status::Busy);
			    CHECK(m.selectField(c.fixture->bind()) == cl::Status::Busy);
			    CHECK(m.setScalar(c.arg, std::uint64_t{999}) == rt::Status::Stale);
			    CHECK(m.callService([](const wb::ServiceResult&) noexcept {
			           }).status == cl::Status::Busy);
			    CHECK(!m.canCallService() && !m.drained());
			    m.cancelEdit();
			    m.clearSelection();
			    m.process(0);
			    CHECK(!m.nextPacket() && *c.reads == 0 && svc::device.calls == 0);
			    CHECK(!m.prepareHandoff(17, 1) && !m.onSendAccepted(17, 1) &&
			          !m.onReply({}, 17, 1));
		    },
		    +[](void* p) noexcept {
			    auto& c = *static_cast<Context*>(p);
			    CHECK(c.fixture->inspector.selectService(
			              c.fixture->inspector.bindService(2).endpoint) == cl::Status::Busy);
			    CHECK(!c.fixture->inspector.nextPacket() && !c.fixture->inspector.drained());
		    }};
		CHECK(m.callService(OwnershipProbe{state}));
		f.pump();
		CHECK(state.moves > 0 && state.callbacks == 1 && state.destructions == 1 && reads == 1);
		CHECK(svc::device.calls == 1 && svc::device.last.u64 == 73);
	}
	{
		svc::Encoded<> f;
		(void)svc::select(f);
		OwnershipState state{&f, +[](void* p) noexcept {
			                     auto& f = *static_cast<svc::Encoded<>*>(p);
			                     f.inspector.close();
			                     f.inspector.process(0);
			                     CHECK(!f.inspector.nextPacket() && !f.inspector.drained());
		                     }};
		CHECK(f.inspector.callService(OwnershipProbe{state}).status == cl::Status::Closed);
		f.inspector.process(0);
		CHECK(state.callbacks == 0 && state.destructions == 1 && svc::device.calls == 0 &&
		      f.inspector.drained());
	}
	{
		svc::Encoded<1> f;
		unsigned destructions = 0;
		CHECK(f.inspector.readField(f.bind(), [&](const im::ReadResult&) noexcept {
			(void)svc::select(f);
			const auto arg = svc::child(f.inspector, svc::root(f.inspector), 7);
			CHECK(f.inspector.setScalar(arg, std::uint64_t{123}) == rt::Status::Ok);
			OwnershipState state{
			    &f, nullptr, +[](void* p) noexcept {
				    auto& f = *static_cast<svc::Encoded<1>*>(p);
				    CHECK(f.inspector.selectService(f.inspector.bindService(2).endpoint) ==
				          cl::Status::Busy);
				    f.inspector.cancelEdit();
				    CHECK(!f.inspector.nextPacket() && !f.inspector.drained());
			    }};
			CHECK(f.inspector.callService(OwnershipProbe{state}).status == cl::Status::QueueFull);
			CHECK(state.callbacks == 0 && state.destructions == 1 && f.inspector.canCallService());
			CHECK(svc::scalar<std::uint64_t>(f.inspector, arg) == 123);
			destructions = state.destructions;
		}));
		f.pump();
		CHECK(destructions == 1 && svc::device.calls == 0);
	}
}

int main()
{
	exactResponse<svc::Encoded<>>();
	exactResponse<svc::Rpc>();
	voidEmptyAndRead<svc::Encoded<>>();
	voidEmptyAndRead<svc::Rpc>();
	capacityAndMemory();
	noStorageVoidEmpty();
	overlap();
	queueAndSnapshot();
	phaseBoundaries();
	staleAndPassive();
	normalReentrancy();
	largeResponse<svc::Encoded<>>();
	largeResponse<svc::Rpc>();
	rpcBoundary();
	ownershipPublication();
	std::printf("Workbench Service: %u checks, 0 failures\n", inspector_fixture::checks);
	return 0;
}
