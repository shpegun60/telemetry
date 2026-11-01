/* Owner-entry gate priorities and runtime result contracts (MIT). */
#include "Fixture.hpp"

namespace {
using namespace dynamic_fixture;

struct Time {
	std::uint32_t now = 0;
	unsigned reads = 0;
	cl::Clock clock{this, +[](void* context) noexcept {
		                auto& time = *static_cast<Time*>(context);
		                ++time.reads;
		                return time.now;
	                }};
};

// Deliberately controlled protocol-free backend. Real wire/correlation behavior
// is exercised by Host.cpp; this double exposes owner-entry side effects exactly.
struct ControlledBackend final : cl::Backend {
	unsigned processes = 0, starts = 0, cancellations = 0, opens = 0, closes = 0;
	unsigned confirmations = 0, releases = 0, replies = 0;
	std::uint32_t generation = 0;
	bool admitted = false, recovery = false, active = false, terminal = false;
	bool prepared = false;
	cl::Operation operation = cl::Operation::FieldRead;
	cl::ResultView outcome{};
	std::array<std::byte, 1> packet{std::byte{0x42}};

	cl::Status start(cl::RequestView request, std::uint32_t) noexcept override
	{
		++starts;
		if (recovery)
			return cl::Status::RecoveryRequired;
		if (!admitted)
			return cl::Status::NotAdmitted;
		if (active || terminal)
			return cl::Status::Busy;
		operation = request.operation;
		active = true;
		prepared = false;
		return cl::Status::Ok;
	}

	void process(std::uint32_t) noexcept override
	{
		++processes;
	}

	bool completed() const noexcept override
	{
		return terminal;
	}

	cl::ResultView result() const noexcept override
	{
		return outcome;
	}

	void releaseResult() noexcept override
	{
		++releases;
		terminal = false;
		outcome = {};
	}

	void cancel(cl::Status reason) noexcept override
	{
		++cancellations;
		if (!active)
			return;
		outcome = {prepared && operation != cl::Operation::FieldRead ? cl::Status::OutcomeUnknown
		                                                             : reason,
		           operation};
		active = false;
		terminal = true;
		if (prepared) {
			recovery = true;
			admitted = false;
		}
	}

	bool ready() const noexcept override
	{
		return admitted && !recovery;
	}

	bool recoveryRequired() const noexcept override
	{
		return recovery;
	}

	std::uint32_t session() const noexcept override
	{
		return generation;
	}

	bool openSession(std::uint32_t next) noexcept override
	{
		++opens;
		if (next == 0 || recovery || active || terminal)
			return false;
		generation = next;
		admitted = true;
		return true;
	}

	bool confirmSessionClosed(std::uint32_t old) noexcept override
	{
		++confirmations;
		if (!recovery || old != generation || active || terminal)
			return false;
		recovery = false;
		return true;
	}

	void close() noexcept override
	{
		++closes;
		if (active)
			cancel(cl::Status::SessionLost);
		if (admitted)
			recovery = true;
		admitted = false;
	}

	cl::Packet nextPacket() noexcept override
	{
		return active && !prepared ? cl::Packet{packet, generation, 1, true} : cl::Packet{};
	}

	bool prepareHandoff(std::uint32_t session, std::uint32_t request) noexcept override
	{
		if (!active || !ready() || prepared || session != generation || request != 1)
			return false;
		prepared = true;
		return true;
	}

	bool onSendAccepted(std::uint32_t session, std::uint32_t request) noexcept override
	{
		return active && prepared && session == generation && request == 1;
	}

	bool onReply(cl::Input, std::uint32_t, std::uint32_t) noexcept override
	{
		++replies;
		return false;
	}

	void finish(cl::Operation reported, cl::Status status = cl::Status::Ok,
	            std::uint8_t dispatch = 0, std::uint8_t endpoint = 0) noexcept
	{
		CHECK(active);
		outcome = {status, reported, dispatch, endpoint, 0, {}};
		active = false;
		terminal = true;
	}
};

struct Rig {
	Metadata metadata;
	Time time;
	ControlledBackend backend;
	dy::DynamicClient<4, 64> client{backend, time.clock};

	void admit(std::uint32_t generation = 1) noexcept
	{
		CHECK(client.admit(metadata.schema, cl::Input{bytes}.first(64), generation) ==
		      cl::Status::Ok);
	}

	dy::EndpointBinding command() noexcept
	{
		const auto endpoint = client.bind(cl::Operation::Command, 2);
		CHECK(endpoint);
		CHECK(endpoint.endpoint.requestType() == 0);
		CHECK(endpoint.endpoint.responseType() == 0);
		return endpoint.endpoint;
	}

	void close() noexcept
	{
		client.close();
		for (unsigned i = 0; i < 8 && !client.drained(); ++i)
			client.process(time.now);
		CHECK(client.drained());
	}
};

void synchronousPriority() noexcept
{
	Rig rig;
	unsigned callbacks = 0;
	auto callback = [&](const dy::DynamicResult&) noexcept {
		++callbacks;
	};
	dy::EndpointBinding absent;
	CHECK(rig.client.submit(absent, {}, callback).status == cl::Status::NotAdmitted);
	CHECK(rig.backend.processes == 0 && rig.backend.starts == 0 && rig.time.reads == 0);
	// The FIFO is still initially unadmitted. No process() has copied this direct
	// backend recovery gate into its cached admission status.
	rig.backend.recovery = true;
	CHECK(rig.client.submit(absent, {}, callback).status == cl::Status::RecoveryRequired);
	CHECK(rig.client.readField(absent, callback).status == cl::Status::RecoveryRequired);
	CHECK(rig.client.writeField(absent, {}, callback).status == cl::Status::RecoveryRequired);
	CHECK(rig.client.command(absent, callback).status == cl::Status::RecoveryRequired);
	CHECK(rig.client.service(absent, callback).status == cl::Status::RecoveryRequired);
	CHECK(rig.client.bind(cl::Operation::Command, 2).status == cl::Status::RecoveryRequired);
	CHECK(rig.client.admit(rig.metadata.schema, cl::Input{bytes}.first(64), 1) ==
	      cl::Status::RecoveryRequired);
	CHECK(rig.client.pending() == 0 && callbacks == 0);
	CHECK(rig.backend.processes == 0 && rig.backend.starts == 0 && rig.backend.opens == 0);
	CHECK(rig.time.reads == 0);
	rig.client.close();
	CHECK(rig.client.submit(absent, {}, callback).status == cl::Status::Closed);
	CHECK(rig.client.readField(absent, callback).status == cl::Status::Closed);
	CHECK(rig.client.command(absent, callback).status == cl::Status::Closed);
	CHECK(rig.client.bind(cl::Operation::Command, 2).status == cl::Status::Closed);
	CHECK(rig.client.admit(rig.metadata.schema, cl::Input{bytes}.first(64), 1) ==
	      cl::Status::Closed);
	CHECK(rig.backend.recoveryRequired());
	CHECK(rig.backend.processes == 0 && rig.backend.starts == 0 && callbacks == 0);
	CHECK(rig.time.reads == 0 && rig.client.pending() == 0);
	rig.close();
}

void recoveryBeforeCachedAdmission() noexcept
{
	Rig rig;
	rig.admit();
	const auto endpoint = rig.command();
	unsigned callbacks = 0;
	rig.backend.recovery = true;
	const auto reads = rig.time.reads;
	CHECK(rig.client
	          .submit(endpoint, {},
	                  [&](const auto&) noexcept {
		                  ++callbacks;
	                  })
	          .status == cl::Status::RecoveryRequired);
	CHECK(rig.client.pending() == 0 && callbacks == 0 && rig.backend.starts == 0);
	CHECK(rig.backend.processes == 0 && rig.time.reads == reads);
	CHECK(rig.client.isCurrent(endpoint)); // Recovery alone does not stale schema identity.
	rig.close();
}

void nullCallbacks() noexcept
{
	void (*callback)(const dy::DynamicResult&) noexcept = nullptr;
	Rig rig;
	dy::EndpointBinding absent;
	CHECK(rig.client.submit(absent, {}, callback).status == cl::Status::NotAdmitted);
	rig.backend.recovery = true;
	CHECK(rig.client.submit(absent, {}, callback).status == cl::Status::RecoveryRequired);
	rig.backend.recovery = false;
	rig.admit();
	const auto command = rig.command();
	const auto field = rig.client.bind(cl::Operation::FieldRead, 0);
	CHECK(field);
	const auto processes = rig.backend.processes, reads = rig.time.reads;
	CHECK(rig.client.submit(command, {}, callback).status == cl::Status::InvalidArgument);
	CHECK(rig.client.command(command, callback).status == cl::Status::InvalidArgument);
	CHECK(rig.client.readField(field.endpoint, callback).status == cl::Status::InvalidArgument);
	CHECK(rig.client.pending() == 0 && rig.backend.starts == 0);
	CHECK(rig.backend.processes == processes && rig.time.reads == reads);

	struct FalseCallable {
		unsigned* calls;

		explicit operator bool() const noexcept
		{
			return false;
		}

		void operator()(const dy::DynamicResult& result) noexcept
		{
			CHECK(result.operation.succeeded());
			++*calls;
		}
	};

	unsigned calls = 0;
	CHECK(rig.client.command(command, FalseCallable{&calls}));
	rig.client.process(0);
	CHECK(rig.backend.starts == 1 && calls == 0);
	rig.backend.finish(cl::Operation::Command);
	rig.client.process(0);
	CHECK(calls == 1 && rig.client.pending() == 0);
	rig.backend.recovery = true;
	CHECK(rig.client.command(command, callback).status == cl::Status::RecoveryRequired);
	rig.client.close();
	CHECK(rig.client.submit(command, {}, callback).status == cl::Status::Closed);
	rig.close();
}

void failedAdmissionAndStickyIdentity() noexcept
{
	Rig rig;
	std::array<std::byte, 64> wrong{};
	CHECK(rig.client.admit(rig.metadata.schema, wrong, 1) == cl::Status::NotAdmitted);
	CHECK(rig.backend.opens == 0 && rig.backend.processes == 0);
	CHECK(rig.client.admit(rig.metadata.schema, cl::Input{bytes}.first(64), 0) ==
	      cl::Status::InvalidArgument);
	CHECK(rig.backend.opens == 0);
	rig.admit(); // Failed attempts neither consume the epoch nor publish a binding.
	const auto endpoint = rig.command();
	CHECK(rig.client.isCurrent(endpoint));
	rig.backend.generation = 2;
	CHECK(!rig.client.isCurrent(endpoint));
	rig.backend.generation = 1;
	CHECK(!rig.client.isCurrent(endpoint));
	CHECK(!rig.client.isCurrent());
	unsigned calls = 0;
	CHECK(rig.client
	          .submit(endpoint, {},
	                  [&](const auto&) noexcept {
		                  ++calls;
	                  })
	          .status == cl::Status::NotAdmitted);
	CHECK(rig.backend.starts == 0 && rig.backend.processes == 0 && calls == 0);
	CHECK(rig.client.confirmQuiescence({1, true, true, true}));
	CHECK(rig.backend.closes == 1 && rig.backend.confirmations == 1);
	rig.admit(2);
	CHECK(rig.client.isCurrent());
	CHECK(!rig.client.isCurrent(endpoint));
	CHECK(rig.client
	          .submit(endpoint, {},
	                  [&](const auto&) noexcept {
		                  ++calls;
	                  })
	          .status == cl::Status::NotAdmitted);
	CHECK(calls == 0 && rig.client.pending() == 0);
	const auto replacement = rig.command();
	CHECK(rig.client.isCurrent(replacement) && replacement.session() == 2);
	rig.close();
}

void checkedOutcomeAndCallbackQuiescence() noexcept
{
	Rig rig;
	rig.admit();
	const auto endpoint = rig.command();
	unsigned calls = 0;
	CHECK(rig.client.command(endpoint, [&](const dy::DynamicResult& result) noexcept {
		++calls;
		CHECK(result.operation.status == cl::Status::Ok);
		CHECK(result.operation.operation == cl::Operation::FieldRead);
		// The declared Command response is Void. Operation mismatch must be
		// classified BEFORE the normal successful-Void NoValue branch.
		CHECK(result.valueStatus == dy::ValueStatus::TypeMismatch);
		CHECK(!result.hasValue());
		rig.client.invalidate();
		const auto closes = rig.backend.closes;
		CHECK(!rig.client.confirmQuiescence({1, true, true, true}));
		CHECK(rig.backend.closes == closes);
		CHECK(rig.client.admit(rig.metadata.schema, cl::Input{bytes}.first(64), 2) ==
		      cl::Status::Busy);
	}));
	rig.client.process(0);
	CHECK(rig.backend.starts == 1 && rig.client.pending() == 1);
	rig.backend.finish(cl::Operation::FieldRead);
	rig.client.process(0);
	CHECK(calls == 1 && rig.client.pending() == 0 && rig.backend.releases == 1);
	CHECK(!rig.client.confirmQuiescence({1, false, true, true}));
	CHECK(!rig.client.confirmQuiescence({1, true, false, true}));
	CHECK(!rig.client.confirmQuiescence({1, true, true, false}));
	CHECK(!rig.client.confirmQuiescence({2, true, true, true}));
	CHECK(rig.backend.closes == 0);
	CHECK(rig.client.confirmQuiescence({1, true, true, true}));
	CHECK(rig.backend.closes == 1 && rig.backend.confirmations == 1);
	CHECK(rig.client.admit(rig.metadata.schema, cl::Input{bytes}.first(64), 1) ==
	      cl::Status::InvalidArgument);
	rig.admit(2);
	CHECK(!rig.client.isCurrent(endpoint));
	rig.close();
}

void failedOutcomeRetainsCurrentIdentity() noexcept
{
	Rig rig;
	rig.admit();
	const auto endpoint = rig.command();
	unsigned calls = 0;
	CHECK(rig.client.command(endpoint, [&](const dy::DynamicResult& result) noexcept {
		++calls;
		CHECK(result.operation.status == cl::Status::MalformedReply);
		CHECK(result.valueStatus == dy::ValueStatus::NoValue);
		CHECK(!result.hasValue());
		CHECK(rig.client.recoveryRequired() && rig.client.isCurrent(endpoint));
	}));
	rig.client.process(0);
	rig.backend.finish(cl::Operation::Command, cl::Status::MalformedReply);
	rig.backend.recovery = true;
	rig.backend.admitted = false;
	rig.client.process(0);
	CHECK(calls == 1 && rig.client.pending() == 0);
	CHECK(rig.backend.cancellations == 0);
	rig.client.invalidate();
	CHECK(rig.client.confirmQuiescence({1, true, true, true}));
	rig.close();
}
} // namespace

int main()
{
	synchronousPriority();
	nullCallbacks();
	recoveryBeforeCachedAdmission();
	failedAdmissionAndStickyIdentity();
	checkedOutcomeAndCallbackQuiescence();
	failedOutcomeRetainsCurrentIdentity();
	std::printf("dynamic client gates: %u checks, 0 failures\n", dynamic_fixture::checks);
}
