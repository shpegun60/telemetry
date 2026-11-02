/* Deterministic real Encoded/RPC Read/Edit/Write/Refresh acceptance. MIT. */
#include "Fixture.hpp"
#include <bit>
#include <cstring>
#include <limits>

using namespace workbench_fixture;

template<class Fixture>
static void richWrite() noexcept
{
	Fixture f;
	auto& model = f.inspector;
	const auto root = read(f);
	CHECK(model.fields().size() == 2 && model.commands().size() == 1 &&
	      model.services().size() == 1);
	const auto wide = child(model, root, 7), signedWide = child(model, root, 8),
	           bounded = child(model, root, 5);
	const auto nested = child(model, child(model, root, 11), 0);
	const auto matrix = child(model, child(model, child(model, root, 13), 1), 0);
	const auto mode = child(model, child(model, child(model, root, 12), 1), 2);
	const auto wideEnum = child(model, root, 16); // follows zero-sized siblings at same offset
	CHECK(model.phase() == wb::Phase::Idle && model.canEdit());
	CHECK(model.beginEdit() == rt::Status::Ok && model.editing() && !model.dirty());
	CHECK(!model.canApply());
	CHECK(model.setScalar(wide, std::uint32_t{7}) == rt::Status::TypeMismatch);
	CHECK(!model.dirty() && edited<std::uint64_t>(model, wide) == UINT64_MAX);
	CHECK(model.setScalar(root, std::uint64_t{7}) == rt::Status::TypeMismatch);
	CHECK(model.setScalar(wide, std::uint64_t{0}) == rt::Status::Ok);
	CHECK(model.dirty() && model.canApply());
	CHECK(model.setScalar(wide, std::uint64_t{UINT64_MAX}) == rt::Status::Ok);
	CHECK(!model.dirty());
	CHECK(model.setScalar(wide, std::uint64_t{9007199254740993ULL}) == rt::Status::Ok);
	CHECK(model.setScalar(signedWide, std::int64_t{INT64_MAX}) == rt::Status::Ok);
	CHECK(model.setScalar(bounded, std::uint32_t{777}) == rt::Status::Ok);
	CHECK(model.setScalar(nested, std::uint16_t{321}) == rt::Status::Ok);
	CHECK(model.setScalar(matrix, std::uint32_t{31337}) == rt::Status::Ok);
	CHECK(model.setScalar(mode, std::int16_t{-123}) == rt::Status::Ok);
	CHECK(model.setScalar(wideEnum, std::uint64_t{1234567890123456789ULL}) == rt::Status::Ok);
	CHECK(edited<std::uint32_t>(model, bounded) == 777);
	CHECK(scalar<std::uint32_t>(model, bounded) == UINT32_MAX);
	CHECK(!model.nextPacket() && owner.writes == 0 && owner.reads == 1);
	unsigned callbacks = 0;
	CHECK(model.apply([&](const wb::ApplyResult& result) noexcept {
		++callbacks;
		CHECK(result.writeCompleted && result.write.succeeded());
		CHECK(result.write.operation == cl::Operation::FieldWrite);
		CHECK(result.refreshAttempted && result.refreshSubmitted && result.refreshCompleted);
		CHECK(result.refreshSubmission == cl::Status::Ok && result.refresh.operation.succeeded());
		CHECK(result.refresh.published && result.refresh.valueStatus == dy::ValueStatus::Ok);
	}));
	CHECK(model.phase() == wb::Phase::Writing && !model.root());
	CHECK(!model.info(root) && !model.editScalar(wide));
	f.pump();
	CHECK(callbacks == 1 && model.phase() == wb::Phase::Idle && !model.editing());
	CHECK(owner.writes == 1 && owner.reads == 2 && owner.commands == 0 && owner.services == 0);
	const auto actual = model.root();
	CHECK(actual);
	CHECK(scalar<std::uint32_t>(model, child(model, actual.value, 5)) ==
	      100); // setter normalization
	CHECK(scalar<std::uint64_t>(model, child(model, actual.value, 7)) == 9007199254740993ULL);
	CHECK(scalar<std::int64_t>(model, child(model, actual.value, 8)) == INT64_MAX);
	CHECK(owner.packet.nested.code == 321 && owner.packet.matrix[1][0] == 31337);
	CHECK(static_cast<std::int16_t>(owner.packet.rows[1].mode) == -123);
	CHECK(static_cast<std::uint64_t>(owner.packet.wide) == 1234567890123456789ULL);
	CHECK(std::bit_cast<std::uint32_t>(owner.packet.f32) == 0x80000000U);
	CHECK(std::bit_cast<std::uint64_t>(owner.packet.f64) == 0x7ff0000000000000ULL);
	CHECK(model.beginEdit() == rt::Status::Ok);
	model.cancelEdit();
	CHECK(!model.editing() && model.snapshotInfo().ready);
	model.close();
	model.process(f.now);
	CHECK(model.drained());
}

template<class Fixture>
static void exactFloatPreservation() noexcept
{
	constexpr std::array<std::uint32_t, 6> floats{0x7fc12345U, 0xffc23456U, 0x7f812345U,
	                                              0xff823456U, 0U,          0x80000000U};
	constexpr std::array<std::uint64_t, 6> doubles{0x7ff8123456789abcULL,
	                                               0xfff823456789abcdULL,
	                                               0x7ff0123456789abcULL,
	                                               0xfff023456789abcdULL,
	                                               0ULL,
	                                               0x8000000000000000ULL};
	for (std::size_t i = 0; i < floats.size(); ++i) {
		Fixture f;
		owner.packet.f32 = std::bit_cast<float>(floats[i]);
		owner.packet.f64 = std::bit_cast<double>(doubles[i]);
		const auto root = read(f);
		CHECK(f.inspector.beginEdit() == rt::Status::Ok);
		CHECK(f.inspector.setScalar(child(f.inspector, root, 1), std::uint8_t{42}) ==
		      rt::Status::Ok);
		unsigned callbacks = 0;
		CHECK(f.inspector.apply([&](const wb::ApplyResult& r) noexcept {
			++callbacks;
			CHECK(r.write.succeeded() && r.refresh.published);
		}));
		f.pump();
		CHECK(callbacks == 1);
		const auto actual = f.inspector.root();
		CHECK(actual);
		const auto fv = f.inspector.scalar(child(f.inspector, actual.value, 9));
		const auto dv = f.inspector.scalar(child(f.inspector, actual.value, 10));
		CHECK(fv && dv);
		CHECK(std::bit_cast<std::uint32_t>(*std::get_if<float>(&fv.value.value)) == floats[i]);
		CHECK(std::bit_cast<std::uint64_t>(*std::get_if<double>(&dv.value.value)) == doubles[i]);
		CHECK(std::bit_cast<std::uint32_t>(owner.packet.f32) == floats[i]);
		CHECK(std::bit_cast<std::uint64_t>(owner.packet.f64) == doubles[i]);
		std::fill(f.port.begin(), f.port.end(), std::byte{0xa5});
		std::fill(f.reply.begin(), f.reply.end(), std::byte{0xa5});
		std::fill(f.scratch.begin(), f.scratch.end(), std::byte{0xa5});
		owner.packet = {};
		const auto owned = f.inspector.scalar(child(f.inspector, actual.value, 9));
		CHECK(owned);
		CHECK(std::bit_cast<std::uint32_t>(*std::get_if<float>(&owned.value.value)) == floats[i]);
		f.inspector.close();
		f.inspector.process(f.now);
		CHECK(f.inspector.drained());
	}
}

static void permissionsAndIdentity() noexcept
{
	Encoded<> f;
	const auto readonly = read(f, 1);
	CHECK(!f.inspector.canEdit() && f.inspector.beginEdit() == rt::Status::TypeMismatch);
	CHECK(f.inspector.info(readonly));
	const auto root = read(f);
	const auto leaf = child(f.inspector, root, 7);
	CHECK(f.inspector.beginEdit() == rt::Status::Ok);
	const auto wrong = f.inspector.setScalar(leaf, std::int64_t{7});
	CHECK(wrong == rt::Status::TypeMismatch && !f.inspector.dirty());
	Encoded<> other;
	const auto otherRoot = read(other);
	const auto foreign = child(other.inspector, otherRoot, 7);
	CHECK(f.inspector.setScalar(foreign, std::uint64_t{7}) == rt::Status::Stale);
	CHECK(f.inspector.setScalar(leaf, std::uint64_t{7}) == rt::Status::Ok);
	CHECK(f.inspector.dirty());
	CHECK(f.metadata.workspace.reset() == rt::Status::Ok);
	CHECK(!f.inspector.editing() && !f.inspector.editScalar(leaf));
	CHECK(f.inspector.setScalar(leaf, std::uint64_t{8}) == rt::Status::Stale);
	CHECK(!f.inspector.apply([](const wb::ApplyResult&) noexcept {
	}));
	CHECK(!f.inspector.nextPacket() && !f.inspector.root());
	f.inspector.process(f.now);
	CHECK(f.inspector.phase() == wb::Phase::Idle);
	f.inspector.close();
	f.inspector.process(f.now);
	CHECK(f.inspector.drained());
	other.inspector.close();
	other.inspector.process(other.now);
	Encoded<4, 32, 512> small;
	(void)read(small);
	CHECK(!small.inspector.canEdit() && small.inspector.beginEdit() == rt::Status::Capacity);
	CHECK(owner.writes == 0 && owner.commands == 0 && owner.services == 0);
	small.inspector.close();
	small.inspector.process(small.now);
}

static void queueRefusalRetainsDraft() noexcept
{
	Encoded<1> f;
	unsigned callbacks = 0, refusedCallbacks = 0;
	im::NodeHandle leaf;
	CHECK(f.inspector.readField(f.bind(), [&](const im::ReadResult& r) noexcept {
		CHECK(r.published);
		leaf = child(f.inspector, f.inspector.root().value, 7);
		CHECK(f.inspector.beginEdit() == rt::Status::Ok);
		CHECK(f.inspector.setScalar(leaf, std::uint64_t{123}) == rt::Status::Ok);
		const auto refused = f.inspector.apply([&](const wb::ApplyResult&) noexcept {
			++refusedCallbacks;
		});
		CHECK(!refused && refused.status == cl::Status::QueueFull);
		CHECK(f.inspector.snapshotInfo().ready && f.inspector.canApply());
		CHECK(edited<std::uint64_t>(f.inspector, leaf) == 123);
	}));
	f.pump();
	CHECK(refusedCallbacks == 0 && owner.writes == 0 && f.inspector.pending() == 0);
	CHECK(f.inspector.setScalar(leaf, std::uint64_t{124}) == rt::Status::Ok);
	CHECK(f.inspector.apply([&](const wb::ApplyResult& r) noexcept {
		++callbacks;
		CHECK(r.writeCompleted && r.write.succeeded());
		CHECK(r.refreshAttempted && !r.refreshSubmitted && !r.refreshCompleted);
		CHECK(r.refreshSubmission == cl::Status::QueueFull);
	}));
	f.pump();
	CHECK(callbacks == 1 && refusedCallbacks == 0 && owner.writes == 1 && owner.packet.u64 == 124);
	CHECK(owner.reads == 1 && !f.inspector.root() && !f.inspector.nextPacket());
	f.inspector.process(f.now + 1);
	CHECK(callbacks == 1 && owner.reads == 1);
	f.inspector.close();
	f.inspector.process(f.now);
	CHECK(f.inspector.drained());
}

static void independentResults() noexcept
{
	{
		Encoded<> f;
		const auto root = read(f);
		owner.refuse = true;
		CHECK(f.inspector.beginEdit() == rt::Status::Ok);
		CHECK(f.inspector.setScalar(child(f.inspector, root, 1), std::uint8_t{2}) ==
		      rt::Status::Ok);
		unsigned callbacks = 0;
		CHECK(f.inspector.apply([&](const wb::ApplyResult& r) noexcept {
			++callbacks;
			CHECK(r.writeCompleted && !r.write.succeeded());
			CHECK(r.write.status == cl::Status::Ok &&
			      r.write.endpointStatus ==
			          static_cast<std::uint8_t>(telemetry::WriteResult::InvalidValue));
			CHECK(!r.refreshAttempted && !r.refreshSubmitted && !r.refreshCompleted);
		}));
		f.pump();
		CHECK(callbacks == 1 && owner.writes == 1 && owner.reads == 1);
	}
	{
		Encoded<> f;
		const auto root = read(f);
		CHECK(f.inspector.beginEdit() == rt::Status::Ok);
		CHECK(f.inspector.setScalar(child(f.inspector, root, 1), std::uint8_t{3}) ==
		      rt::Status::Ok);
		unsigned callbacks = 0;
		const auto completed = [&](const wb::ApplyResult& r) noexcept {
			++callbacks;
			CHECK(r.write.succeeded() && r.refresh.published);
		};
		for (const auto timeout : {std::uint32_t{0}, cl::maxTimeoutMs + 1}) {
			const auto invalidWrite = f.inspector.apply(completed, timeout, 100);
			const auto invalidRefresh = f.inspector.apply(completed, 100, timeout);
			CHECK(!invalidWrite && invalidWrite.status == cl::Status::InvalidArgument);
			CHECK(!invalidRefresh && invalidRefresh.status == cl::Status::InvalidArgument);
			CHECK(callbacks == 0 && f.inspector.canApply() && f.inspector.snapshotInfo().ready &&
			      f.inspector.pending() == 0);
		}
		void (*nullCallback)(const wb::ApplyResult&) noexcept = nullptr;
		const auto null = f.inspector.apply(nullCallback);
		CHECK(!null && null.status == cl::Status::InvalidArgument && f.inspector.canApply());
		CHECK(owner.writes == 0 && !f.inspector.nextPacket());
		CHECK(f.inspector.apply(completed, 100, 100));
		f.pump();
		CHECK(callbacks == 1 && owner.writes == 1 && owner.reads == 2);
	}
	{
		Encoded<> f;
		const auto root = read(f);
		CHECK(f.inspector.beginEdit() == rt::Status::Ok);
		CHECK(f.inspector.setScalar(child(f.inspector, root, 1), std::uint8_t{4}) ==
		      rt::Status::Ok);
		unsigned callbacks = 0;
		CHECK(f.inspector.apply(
		    [&](const wb::ApplyResult& r) noexcept {
			    ++callbacks;
			    CHECK(r.write.succeeded() && r.refreshSubmitted && r.refreshCompleted);
			    CHECK(!r.refresh.published && r.refresh.operation.status == cl::Status::Timeout);
		    },
		    100, 20));
		(void)f.send(f.offered());
		CHECK(f.inspector.phase() == wb::Phase::Refreshing && callbacks == 0);
		(void)f.send(f.offered(), false);
		f.now = 20;
		f.inspector.process(f.now);
		CHECK(callbacks == 1 && owner.writes == 1 && owner.reads == 2 && !f.inspector.root());
		f.inspector.process(21);
		CHECK(callbacks == 1);
	}
}

static void deadlinePhases() noexcept
{
	for (unsigned phase = 0; phase != 3; ++phase) {
		Encoded<> f;
		const auto root = read(f);
		CHECK(f.inspector.beginEdit() == rt::Status::Ok);
		CHECK(f.inspector.setScalar(child(f.inspector, root, 1), std::uint8_t{5}) ==
		      rt::Status::Ok);
		unsigned callbacks = 0;
		cl::Packet packet{};
		std::size_t responseBytes = 0;
		CHECK(f.inspector.apply(
		    [&](const wb::ApplyResult& r) noexcept {
			    ++callbacks;
			    CHECK(r.writeCompleted && !r.refreshAttempted);
			    CHECK(r.write.status ==
			          (phase == 2 ? cl::Status::OutcomeUnknown : cl::Status::Timeout));
		    },
		    20, 20));
		if (phase)
			packet = f.offered();
		if (phase == 2)
			responseBytes = f.send(packet, false);
		f.now = 20;
		f.inspector.process(f.now);
		CHECK(callbacks == 1 && f.inspector.pending() == 0 &&
		      owner.writes == (phase == 2 ? 1U : 0U));
		CHECK(f.inspector.recoveryRequired() == (phase == 2));
		CHECK(!f.inspector.nextPacket());
		if (phase == 2)
			CHECK(!f.inspector.onReply(cl::Input{f.reply}.first(responseBytes), packet.session,
			                           packet.requestId));
		f.inspector.process(100);
		CHECK(callbacks == 1);
		std::printf("Workbench timeout phase=%s callbacks=%u pending=%zu writes=%u recovery=%u\n",
		            phase == 0   ? "queued"
		            : phase == 1 ? "offered"
		                         : "prepared",
		            callbacks, f.inspector.pending(), owner.writes,
		            unsigned(f.inspector.recoveryRequired()));
		f.inspector.close();
		f.inspector.process(f.now);
		CHECK(f.inspector.drained());
	}
}

static void invalidateAndReentrancy() noexcept
{
	for (unsigned prepared = 0; prepared != 2; ++prepared) {
		Encoded<> f;
		const auto root = read(f);
		CHECK(f.inspector.beginEdit() == rt::Status::Ok);
		CHECK(f.inspector.setScalar(child(f.inspector, root, 1), std::uint8_t{6}) ==
		      rt::Status::Ok);
		unsigned callbacks = 0;
		CHECK(f.inspector.apply([&](const wb::ApplyResult& r) noexcept {
			++callbacks;
			if (prepared) {
				CHECK(r.write.succeeded() && r.refreshAttempted && !r.refreshSubmitted);
				CHECK(r.refreshSubmission == cl::Status::NotAdmitted);
			} else
				CHECK(r.write.status == cl::Status::Cancelled && !r.refreshAttempted);
		}));
		cl::Packet packet{};
		std::size_t response = 0;
		if (prepared) {
			packet = f.offered();
			response = f.send(packet, false);
		}
		CHECK(f.metadata.workspace.reset() == rt::Status::Ok);
		CHECK(!f.inspector.nextPacket());
		if (prepared)
			CHECK(f.inspector.onReply(cl::Input{f.reply}.first(response), packet.session,
			                          packet.requestId));
		f.inspector.process(f.now);
		CHECK(callbacks == 1 && owner.writes == prepared && !f.inspector.root());
		f.inspector.close();
		f.inspector.process(f.now);
		CHECK(f.inspector.drained());
	}
	{
		Encoded<> f;
		const auto root = read(f);
		CHECK(f.inspector.beginEdit() == rt::Status::Ok);
		CHECK(f.inspector.setScalar(child(f.inspector, root, 1), std::uint8_t{7}) ==
		      rt::Status::Ok);
		unsigned callbacks = 0;
		CHECK(f.inspector.apply([&](const wb::ApplyResult& r) noexcept {
			++callbacks;
			CHECK(r.write.succeeded() && r.refreshSubmission == cl::Status::Cancelled &&
			      !r.refreshSubmitted);
		}));
		CHECK(f.inspector.selectField(f.bind(1)) == cl::Status::Ok);
		f.pump();
		CHECK(callbacks == 1 && owner.reads == 1 && f.inspector.snapshotInfo().fieldId == 1);
	}
	{
		Encoded<> f;
		const auto root = read(f);
		CHECK(f.inspector.beginEdit() == rt::Status::Ok);
		CHECK(f.inspector.setScalar(child(f.inspector, root, 1), std::uint8_t{8}) ==
		      rt::Status::Ok);
		unsigned callbacks = 0;
		CHECK(f.inspector.apply([&](const wb::ApplyResult& r) noexcept {
			++callbacks;
			CHECK(r.write.succeeded() && r.refresh.published);
			CHECK(f.inspector.beginEdit() == rt::Status::Ok);
			CHECK(f.inspector.setScalar(child(f.inspector, f.inspector.root().value, 1),
			                            std::uint8_t{9}) == rt::Status::Ok);
			const auto refused = f.inspector.apply([](const wb::ApplyResult&) noexcept {
			});
			CHECK(!refused && refused.status == cl::Status::Busy);
			f.inspector.close();
			f.inspector.process(f.now);
			CHECK(!f.inspector.drained());
		}));
		f.pump();
		f.inspector.process(f.now);
		CHECK(callbacks == 1 && f.inspector.drained());
		const auto closed = f.inspector.apply([](const wb::ApplyResult&) noexcept {
		});
		CHECK(!closed && closed.status == cl::Status::Closed);
	}
}

static void cancellationPhases() noexcept
{
	for (unsigned prepared = 0; prepared != 2; ++prepared) {
		Encoded<> f;
		const auto root = read(f);
		CHECK(f.inspector.beginEdit() == rt::Status::Ok);
		CHECK(f.inspector.setScalar(child(f.inspector, root, 1), std::uint8_t{10}) ==
		      rt::Status::Ok);
		unsigned callbacks = 0;
		const auto submitted = f.inspector.apply([&](const wb::ApplyResult& r) noexcept {
			++callbacks;
			CHECK(r.write.status ==
			      (prepared ? cl::Status::OutcomeUnknown : cl::Status::Cancelled));
			CHECK(!r.refreshAttempted);
		});
		CHECK(submitted);
		if (prepared)
			(void)f.send(f.offered(), false);
		CHECK(f.inspector.cancel(submitted.handle));
		f.inspector.process(f.now);
		CHECK(callbacks == 1 && f.inspector.pending() == 0 && owner.writes == prepared);
		CHECK(f.inspector.recoveryRequired() == bool(prepared));
		CHECK(!f.inspector.cancel(submitted.handle) && !f.inspector.nextPacket());
		f.inspector.process(f.now + 1);
		CHECK(callbacks == 1);
	}
}

static unsigned functionCalls = 0;

static void functionCompletion(const wb::ApplyResult& result) noexcept
{
	CHECK(result.write.succeeded() && result.refresh.published);
	++functionCalls;
}

static void functionReferences() noexcept
{
	Encoded<> f;
	const auto root = read(f);
	CHECK(f.inspector.beginEdit() == rt::Status::Ok);
	CHECK(f.inspector.setScalar(child(f.inspector, root, 1), std::uint8_t{10}) == rt::Status::Ok);
	functionCalls = 0;
	CHECK(f.inspector.apply(functionCompletion));
	f.pump();
	CHECK(functionCalls == 1 && owner.writes == 1);
}

int main()
{
	functionReferences();
	richWrite<Encoded<>>();
	richWrite<Rpc>();
	exactFloatPreservation<Encoded<>>();
	exactFloatPreservation<Rpc>();
	permissionsAndIdentity();
	queueRefusalRetainsDraft();
	independentResults();
	deadlinePhases();
	invalidateAndReentrancy();
	cancellationPhases();
	std::printf("Workbench Host: %u checks, 0 failures\n", inspector_fixture::checks);
	return 0;
}
