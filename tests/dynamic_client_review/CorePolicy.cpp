/*
 * @file CorePolicy.cpp
 * @brief Independent admission-policy, execution-evidence and reentrant lifecycle checks.
 * @author Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT
 */
#include "../async_client/Fixture.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>

namespace {
namespace cl = telemetry::client;
namespace af = async_fixture;
unsigned checks = 0;

void check(bool ok, int line)
{
	++checks;
	if (!ok) {
		std::fprintf(stderr, "independent core policy check %u failed at line %d\n", checks, line);
		std::exit(1);
	}
}

#define CHECK(...) check(bool(__VA_ARGS__), __LINE__)

struct Policy {
	bool* live;
	static constexpr bool allowTyped = false;

	bool current() const noexcept
	{
		return *live;
	}

	bool acceptsDescriptor(cl::Input header) const noexcept
	{
		return *live && header.size() == af::Codec::header.size() &&
		       std::equal(header.begin(), header.end(), af::Codec::header.begin());
	}
};

class Backend : public af::Backend {
public:
	std::array<std::byte, 1> frame{std::byte{0x7b}};
	unsigned peeks = 0, prepares = 0, cancels = 0, replies = 0, processes = 0;
	std::uint32_t packetId = 1;
	bool mayExecute = true, rejectPrepare = false, authorized = false;

	cl::Status start(cl::RequestView view, std::uint32_t now) noexcept override
	{
		const auto result = af::Backend::start(view, now);
		if (result == cl::Status::Ok)
			authorized = false;
		return result;
	}

	cl::Packet nextPacket() noexcept override
	{
		++peeks;
		return active ? cl::Packet{frame, generation, packetId, mayExecute} : cl::Packet{};
	}

	void process(std::uint32_t now) noexcept override
	{
		++processes;
		af::Backend::process(now);
	}

	bool prepareHandoff(std::uint32_t session, std::uint32_t id) noexcept override
	{
		++prepares;
		if (!active || session != generation || id != packetId || rejectPrepare)
			return false;
		authorized = authorized || mayExecute;
		return true;
	}

	void cancel(cl::Status reason) noexcept override
	{
		++cancels;
		if (authorized && request.operation != cl::Operation::FieldRead)
			reason = cl::Status::OutcomeUnknown;
		af::Backend::cancel(reason);
	}

	bool onSendAccepted(std::uint32_t session, std::uint32_t id) noexcept override
	{
		return active && session == generation && id == packetId;
	}

	bool onReply(cl::Input, std::uint32_t session, std::uint32_t id) noexcept override
	{
		++replies;
		if (!active || session != generation || id != packetId)
			return false;
		finish();
		return true;
	}
};

using Client =
    cl::DeviceClient<af::Codec, 4, 64, 128, 64, alignof(std::max_align_t), cl::NoLock, Policy>;

struct AnyCallback {
	template<class T>
	void operator()(const T&) const noexcept
	{}
};

struct EmptyService {
	static constexpr unsigned category = 3;
	using Request = void;
	using Response = std::uint32_t;
	std::uint32_t id = 46;
};
template<class C>
concept HasRead = requires(C& c) { c.read(af::Field{}, AnyCallback{}); };
template<class C>
concept HasWrite = requires(C& c) { c.write(af::Field{}, std::uint32_t{}, AnyCallback{}); };
template<class C>
concept HasCommand = requires(C& c) { c.command(af::Command{}, std::uint32_t{}, AnyCallback{}); };
template<class C>
concept HasEmptyCommand = requires(C& c) { c.command(af::EmptyCommand{}, AnyCallback{}); };
template<class C>
concept HasService = requires(C& c) { c.service(af::Service{}, std::uint32_t{}, AnyCallback{}); };
template<class C>
concept HasEmptyService = requires(C& c) { c.service(EmptyService{}, AnyCallback{}); };
static_assert(!HasRead<Client> && !HasWrite<Client> && !HasCommand<Client> &&
              !HasEmptyCommand<Client> && !HasService<Client> && !HasEmptyService<Client>);
using StaticClient = cl::DeviceClient<af::Codec, 4, 64, 128>;
static_assert(HasRead<StaticClient> && HasWrite<StaticClient> && HasCommand<StaticClient> &&
              HasEmptyCommand<StaticClient> && HasService<StaticClient> &&
              HasEmptyService<StaticClient>);

struct Harness {
	bool live = true, invalidateInCallback = false;
	Backend backend;
	Client core{backend, af::clock, Policy{&live}};
	std::array<cl::Status, 4> statuses{};
	unsigned callbacks = 0;
	cl::Submission nested{};

	Harness()
	{
		af::now.store(0);
		CHECK(core.admitDescriptor(af::Codec::header, 1) == cl::Status::Ok);
	}

	cl::Submission submit(std::uint32_t timeout = 100)
	{
		return core.submitEncoded(
		    {cl::Operation::Command, 42, {}, 0},
		    [this](const cl::ResultView& result) noexcept {
			    statuses[callbacks++] = result.status;
			    if (invalidateInCallback) {
				    invalidateInCallback = false;
				    live = false;
				    nested = submit();
				    CHECK(!core.nextPacket());
				    CHECK(!core.prepareHandoff(1, backend.packetId));
				    CHECK(!core.onReply({}, 1, backend.packetId));
				    core.process(0);
			    }
		    },
		    timeout);
	}

	void drain()
	{
		core.close();
		for (unsigned i = 0; i < 6 && !core.drained(); ++i)
			core.process(af::now.load());
		CHECK(core.drained());
		CHECK(core.pending() == 0);
	}
};

void rejectedAndNonExecuting()
{
	for (unsigned mode = 0; mode != 3; ++mode) {
		Harness h;
		CHECK(h.submit());
		const auto packet = h.core.nextPacket();
		CHECK(packet);
		if (mode == 0) {
			h.backend.rejectPrepare = true;
			CHECK(!h.core.prepareHandoff(1, packet.requestId));
		} else if (mode == 1) {
			h.backend.mayExecute = false;
			CHECK(h.core.prepareHandoff(1, packet.requestId));
		} else {
			CHECK(!h.core.prepareHandoff(1, packet.requestId + 1));
		}
		h.live = false;
		CHECK(!h.core.nextPacket());
		CHECK(h.callbacks == 1 && h.statuses[0] == cl::Status::Cancelled);
		CHECK(h.backend.cancels == 1 && !h.backend.authorized);
		CHECK(!h.core.prepareHandoff(1, packet.requestId));
		CHECK(h.backend.starts == 1);
		CHECK(h.submit().status == cl::Status::NotAdmitted);
		h.drain();
	}
}

void executionEvidenceIsSticky()
{
	for (bool accept : {false, true}) {
		Harness h;
		CHECK(h.submit());
		const auto first = h.core.nextPacket();
		CHECK(first && first.mayExecute);
		CHECK(h.core.prepareHandoff(1, first.requestId));
		if (accept)
			CHECK(h.core.onSendAccepted(1, first.requestId));
		h.backend.packetId = 2;
		h.backend.mayExecute = false;
		const auto second = h.core.nextPacket();
		CHECK(second && !second.mayExecute);
		CHECK(h.core.prepareHandoff(1, second.requestId));
		h.live = false;
		CHECK(!h.core.nextPacket());
		CHECK(h.callbacks == 0 && h.backend.cancels == 0);
		CHECK(!h.core.prepareHandoff(1, second.requestId));
		af::now.store(100);
		h.core.process(100);
		CHECK(h.callbacks == 1 && h.statuses[0] == cl::Status::OutcomeUnknown);
		CHECK(h.backend.cancels == 1);
		h.drain();
	}
}

void stalePassiveCompletionAndDeadline()
{
	for (unsigned mode = 0; mode != 3; ++mode) {
		Harness h;
		CHECK(h.submit());
		const auto packet = h.core.nextPacket();
		CHECK(packet && h.core.prepareHandoff(1, packet.requestId));
		h.live = false;
		if (mode == 0) {
			af::now.store(99);
			CHECK(h.core.onReply({}, 1, packet.requestId));
			CHECK(h.callbacks == 1 && h.statuses[0] == cl::Status::Ok);
		} else if (mode == 1) {
			af::now.store(100);
			CHECK(!h.core.onReply({}, 1, packet.requestId));
			CHECK(h.callbacks == 1 && h.statuses[0] == cl::Status::OutcomeUnknown);
		} else {
			h.backend.finish();
			af::now.store(100);
			h.core.process(100);
			CHECK(h.callbacks == 1 && h.statuses[0] == cl::Status::Ok);
			CHECK(h.backend.cancels == 0);
		}
		h.core.process(101);
		CHECK(h.callbacks == 1);
		h.drain();
	}
}

void reentrantInvalidation()
{
	for (unsigned entry = 0; entry != 3; ++entry) {
		Harness h;
		CHECK(h.submit());
		CHECK(h.submit());
		h.core.process(0);
		CHECK(h.backend.starts == 1);
		h.backend.finish();
		h.invalidateInCallback = true;
		if (entry == 0)
			CHECK(!h.core.nextPacket());
		else if (entry == 1)
			CHECK(!h.core.prepareHandoff(1, 1));
		else
			CHECK(!h.core.onReply({}, 1, 1));
		CHECK(h.nested.status == cl::Status::NotAdmitted);
		for (unsigned i = 0; i != 3; ++i)
			h.core.process(0);
		CHECK(h.callbacks == 2);
		CHECK(h.statuses[0] == cl::Status::Ok && h.statuses[1] == cl::Status::Cancelled);
		CHECK(h.backend.starts == 1);
		h.drain();
	}
}

void precedenceAndPerRequestReset()
{
	{
		Harness h;
		CHECK(h.submit());
		h.core.process(0);
		h.live = false;
		af::now.store(100);
		h.core.process(100);
		CHECK(h.callbacks == 1 && h.statuses[0] == cl::Status::Timeout);
		h.backend.recovery = true;
		CHECK(h.submit().status == cl::Status::RecoveryRequired);
		h.core.close();
		CHECK(h.submit().status == cl::Status::Closed);
		h.drain();
	}
	{
		Harness h;
		CHECK(h.submit());
		CHECK(h.submit());
		const auto packet = h.core.nextPacket();
		CHECK(h.core.prepareHandoff(1, packet.requestId));
		CHECK(h.core.onReply({}, 1, packet.requestId));
		h.core.process(0);
		CHECK(h.backend.starts == 2);
		h.live = false;
		h.core.process(0);
		CHECK(h.callbacks == 2 && h.statuses[1] == cl::Status::Cancelled);
		h.drain();
	}
}

void staticCompatibility()
{
	af::now.store(0);
	Backend backend;
	cl::DeviceClient<af::Codec, 2, 64> core{backend, af::clock};
	auto wrong = af::Codec::header;
	wrong[63] = std::byte{1};
	CHECK(core.admitDescriptor(wrong, 1) == cl::Status::NotAdmitted);
	CHECK(core.admitDescriptor(af::Codec::header, 1) == cl::Status::Ok);
	unsigned completed = 0;
	CHECK(core.read(af::Field{}, [&](const cl::Result<std::uint32_t>& result) noexcept {
		CHECK(result && result.value() == 99);
		++completed;
	}));
	core.process(0);
	CHECK(backend.starts == 1);
	backend.finish();
	core.process(0);
	CHECK(completed == 1);
	core.close();
	core.process(0);
	CHECK(core.drained());
}

template<class Admission>
struct DirectRevocation {
	bool live = true;
	Backend backend;
	using Core = cl::DeviceClient<af::Codec, 4, 64, 128, 64, alignof(std::max_align_t), cl::NoLock,
	                              Admission>;
	Core core{backend, af::clock, policy()};
	std::array<cl::Status, 4> statuses{};
	std::array<unsigned, 4> completions{};

	Admission policy() noexcept
	{
		if constexpr (std::is_same_v<Admission, Policy>)
			return Policy{&live};
		else
			return {};
	}

	DirectRevocation()
	{
		af::now.store(0);
		CHECK(core.admitDescriptor(af::Codec::header, 1) == cl::Status::Ok);
	}

	cl::Submission submit(unsigned tag = 0)
	{
		return core.submitEncoded(
		    {cl::Operation::Command, 42, {}, 0},
		    [this, tag](const cl::ResultView& result) noexcept {
			    statuses[tag] = result.status;
			    ++completions[tag];
		    },
		    100);
	}

	void drain()
	{
		core.close();
		for (unsigned i = 0; i != 6 && !core.drained(); ++i)
			core.process(af::now.load());
		CHECK(core.drained() && core.pending() == 0);
	}
};

template<class Admission>
void directQueuedRevocation()
{
	DirectRevocation<Admission> h;
	CHECK(h.submit(0) && h.submit(1));
	h.core.invalidateAdmission();
	h.core.invalidateAdmission();
	CHECK(h.live && h.policy().current());
	CHECK(h.backend.ready() && h.backend.starts == 0 && h.backend.cancels == 0);
	CHECK(h.completions[0] == 0 && h.completions[1] == 0);
	CHECK(h.submit(2).status == cl::Status::NotAdmitted);
	CHECK(h.core.admitDescriptor(af::Codec::header, 2) == cl::Status::Busy);
	CHECK(!h.core.nextPacket());
	CHECK(!h.core.prepareHandoff(1, 1));
	for (unsigned i = 0; i != 3; ++i)
		h.core.process(0);
	CHECK(h.backend.starts == 0 && h.backend.prepares == 0);
	CHECK(h.completions[0] == 1 && h.completions[1] == 1);
	CHECK(h.statuses[0] == cl::Status::Cancelled && h.statuses[1] == cl::Status::Cancelled);
	auto wrong = af::Codec::header;
	wrong[63] = std::byte{1};
	CHECK(h.core.admitDescriptor(wrong, 2) == cl::Status::NotAdmitted);
	CHECK(h.submit(2).status == cl::Status::NotAdmitted);
	CHECK(h.core.admitDescriptor(af::Codec::header, 1) == cl::Status::InvalidArgument);
	CHECK(h.submit(2).status == cl::Status::NotAdmitted);
	CHECK(h.core.admitDescriptor(af::Codec::header, 2) == cl::Status::Ok);
	CHECK(h.submit(2));
	const auto packet = h.core.nextPacket();
	CHECK(packet && packet.session == 2 && h.backend.starts == 1);
	CHECK(h.core.prepareHandoff(packet.session, packet.requestId));
	CHECK(h.core.onReply({}, packet.session, packet.requestId));
	CHECK(h.completions[2] == 1 && h.statuses[2] == cl::Status::Ok);
	h.drain();
}

template<class Admission>
void directUnsentRevocation()
{
	for (unsigned phase = 0; phase != 3; ++phase) {
		DirectRevocation<Admission> h;
		CHECK(h.submit(0) && h.submit(1));
		h.core.process(0);
		CHECK(h.backend.starts == 1);
		h.backend.mayExecute = phase != 1;
		const auto packet = h.core.nextPacket();
		CHECK(packet);
		if (phase == 1)
			CHECK(h.core.prepareHandoff(1, packet.requestId));
		else if (phase == 2) {
			h.backend.rejectPrepare = true;
			CHECK(!h.core.prepareHandoff(1, packet.requestId));
		}
		const auto prepares = h.backend.prepares;
		h.core.invalidateAdmission();
		CHECK(h.live && h.policy().current() && h.backend.ready());
		CHECK(h.completions[0] == 0 && h.backend.cancels == 0);
		CHECK(!h.core.prepareHandoff(1, packet.requestId));
		CHECK(!h.core.nextPacket());
		for (unsigned i = 0; i != 3; ++i)
			h.core.process(0);
		CHECK(h.backend.prepares == prepares && h.backend.starts == 1);
		CHECK(h.backend.cancels == 1 && !h.backend.authorized);
		CHECK(h.completions[0] == 1 && h.statuses[0] == cl::Status::Cancelled);
		CHECK(h.completions[1] == 1 && h.statuses[1] == cl::Status::Cancelled);
		CHECK(h.submit(2).status == cl::Status::NotAdmitted);
		h.drain();
	}
}

template<class Admission>
void directAuthorizedRevocation()
{
	for (bool accept : {false, true}) {
		for (unsigned terminal = 0; terminal != 3; ++terminal) {
			DirectRevocation<Admission> h;
			CHECK(h.submit(0) && h.submit(1));
			const auto first = h.core.nextPacket();
			CHECK(first && first.mayExecute && h.core.prepareHandoff(1, first.requestId));
			CHECK(h.backend.authorized);
			if (accept)
				CHECK(h.core.onSendAccepted(1, first.requestId));
			// A later non-executing RPC-style packet cannot erase final-WRITE evidence.
			h.backend.packetId = 2;
			h.backend.mayExecute = false;
			const auto later = h.core.nextPacket();
			CHECK(later && !later.mayExecute && h.core.prepareHandoff(1, later.requestId));
			const auto prepares = h.backend.prepares;
			h.core.invalidateAdmission();
			CHECK(h.live && h.policy().current());
			CHECK(h.completions[0] == 0 && h.backend.cancels == 0);
			CHECK(!h.core.nextPacket());
			CHECK(!h.core.prepareHandoff(1, later.requestId));
			CHECK(h.backend.prepares == prepares && h.backend.starts == 1);
			CHECK(h.completions[0] == 0 && h.backend.cancels == 0);
			CHECK(h.completions[1] == 1 && h.statuses[1] == cl::Status::Cancelled);
			if (terminal == 0) {
				af::now.store(99);
				CHECK(h.core.onReply({}, 1, later.requestId));
				CHECK(h.statuses[0] == cl::Status::Ok);
			} else if (terminal == 1) {
				af::now.store(100);
				h.core.process(100);
				CHECK(h.statuses[0] == cl::Status::OutcomeUnknown);
				CHECK(!h.core.onReply({}, 1, later.requestId));
			} else {
				h.backend.finish();
				af::now.store(100);
				h.core.process(100);
				CHECK(h.statuses[0] == cl::Status::Ok && h.backend.cancels == 0);
			}
			CHECK(h.completions[0] == 1);
			h.core.process(101);
			CHECK(h.completions[0] == 1 && h.completions[1] == 1);
			CHECK(h.submit(2).status == cl::Status::NotAdmitted);
			h.drain();
		}
	}
}

template<class Admission>
void directReadinessAndRecovery()
{
	DirectRevocation<Admission> h;
	h.core.invalidateAdmission();
	h.backend.opened = false;
	h.core.process(0);
	h.backend.opened = true;
	h.core.process(0);
	CHECK(h.live && h.policy().current() && h.backend.ready());
	CHECK(!h.core.nextPacket() && h.submit().status == cl::Status::NotAdmitted);
	h.backend.recovery = true;
	const auto processes = h.backend.processes;
	h.core.invalidateAdmission();
	CHECK(h.backend.processes == processes && h.backend.starts == 0);
	CHECK(h.submit().status == cl::Status::RecoveryRequired);
	CHECK(h.core.admitDescriptor(af::Codec::header, 2) == cl::Status::RecoveryRequired);
	CHECK(h.core.confirmSessionClosed(1));
	CHECK(!h.core.nextPacket());
	CHECK(h.submit().status != cl::Status::Ok);
	CHECK(h.core.admitDescriptor(af::Codec::header, 2) == cl::Status::Ok);
	CHECK(h.submit());
	h.backend.recovery = true;
	h.core.close();
	h.core.invalidateAdmission();
	CHECK(h.submit(1).status == cl::Status::Closed);
	h.drain();
	CHECK(h.completions[0] == 1 && h.statuses[0] == cl::Status::Cancelled);
	CHECK(h.backend.starts == 0);
}

template<class Admission>
void directRevocation()
{
	directQueuedRevocation<Admission>();
	directUnsentRevocation<Admission>();
	directAuthorizedRevocation<Admission>();
	directReadinessAndRecovery<Admission>();
}
} // namespace

int main()
{
	rejectedAndNonExecuting();
	executionEvidenceIsSticky();
	stalePassiveCompletionAndDeadline();
	reentrantInvalidation();
	precedenceAndPerRequestReset();
	staticCompatibility();
	directRevocation<Policy>();
	directRevocation<cl::StaticAdmission<af::Codec>>();
	std::printf("independent core policy: checks=%u failures=0\n", checks);
}
