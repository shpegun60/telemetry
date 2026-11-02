/* FIFO, ownership, cancellation and drain qualification. Authors: Ruslan Kovtun (shpegun60), codexAi. MIT. */
#include "Fixture.hpp"
#include <cstdio>
#include <cstdlib>
#ifndef ASYNC_NO_THREADS
#include <thread>
#include <mutex>
#endif

namespace af = async_fixture;
namespace cl = telemetry::client;
static unsigned checks = 0, failures = 0;
#define CHECK(value)                                                                               \
	do {                                                                                           \
		++checks;                                                                                  \
		if (!(value)) {                                                                            \
			++failures;                                                                            \
			std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #value);                          \
		}                                                                                          \
	} while (false)
using Client = cl::DeviceClient<af::Codec, 4, 16>;

static unsigned functionCalls = 0;

static void typedFunction(const cl::Result<std::uint32_t>& result) noexcept
{
	CHECK(result.hasValue() && result.value() == 99);
	++functionCalls;
}

static void encodedFunction(const cl::ResultView& result) noexcept
{
	CHECK(result.succeeded() && result.payload.size() == 4);
	++functionCalls;
}

static void functionReferences()
{
	af::now = 0;
	af::Backend backend;
	Client client{backend, af::clock};
	functionCalls = 0;
	CHECK(client.admitDescriptor(af::Codec::header, 1) == cl::Status::Ok);
	// Pass function references by name, without converting them to pointers.
	CHECK(client.read(af::Field{}, typedFunction));
	client.process(0);
	backend.finish();
	client.process(0);
	CHECK(functionCalls == 1);
	CHECK(client.submitEncoded({cl::Operation::FieldRead, 42, {}, 4}, encodedFunction));
	client.process(0);
	backend.finish();
	client.process(0);
	CHECK(functionCalls == 2 && client.pending() == 0 && backend.starts == 2);
	client.close();
	client.process(0);
	CHECK(client.drained());
}

static void nullCallbacks()
{
	af::now = 0;
	af::Backend backend;
	Client client{backend, af::clock};
	void (*number)(const cl::Result<std::uint32_t>&) noexcept = nullptr;
	void (*empty)(const cl::Result<void>&) noexcept = nullptr;
	void (*raw)(const cl::ResultView&) noexcept = nullptr;
	const cl::RequestView request{cl::Operation::FieldRead, 42, {}, 4};
	// Argument checks precede queue reservation, just like an invalid timeout.
	CHECK(client.submitEncoded(request, raw).status == cl::Status::InvalidArgument);
	CHECK(client.admitDescriptor(af::Codec::header, 1) == cl::Status::Ok);
	CHECK(client.read(af::Field{}, number).status == cl::Status::InvalidArgument);
	const auto constNumber = number;
	const auto constRaw = raw;
	CHECK(client.read(af::Field{}, constNumber).status == cl::Status::InvalidArgument);
	CHECK(client.submitEncoded(request, constRaw).status == cl::Status::InvalidArgument);
	CHECK(client.write(af::Field{}, 7U, empty).status == cl::Status::InvalidArgument);
	CHECK(client.command(af::Command{}, 7U, empty).status == cl::Status::InvalidArgument);
	CHECK(client.command(af::EmptyCommand{}, empty).status == cl::Status::InvalidArgument);
	CHECK(client.service(af::Service{}, 7U, number).status == cl::Status::InvalidArgument);
	CHECK(client.submitEncoded(request, raw).status == cl::Status::InvalidArgument);
	CHECK(client.pending() == 0 && backend.starts == 0);

	// A valid user functor may have a false bool conversion; it is still callable.
	struct FalseCallable {
		unsigned* calls;

		explicit operator bool() const noexcept
		{
			return false;
		}

		void operator()(const cl::Result<std::uint32_t>& result) noexcept
		{
			CHECK(result.hasValue() && result.value() == 99);
			++*calls;
		}
	};

	unsigned calls = 0;
	CHECK(client.read(af::Field{}, FalseCallable{&calls}));
	client.process(0);
	CHECK(backend.starts == 1 && client.pending() == 1 && calls == 0);
	backend.finish();
	client.process(0);
	CHECK(calls == 1 && client.pending() == 0);
	client.close();
	CHECK(client.submitEncoded(request, raw).status == cl::Status::InvalidArgument);
	client.process(0);
	CHECK(client.drained());
}

static void lifecycle()
{
	af::now = 10;
	af::Backend backend;
	Client client{backend, af::clock};
	unsigned callbacks = 0, value = 7;
	CHECK(client
	          .read(af::Field{},
	                [&](const auto&) noexcept {
		                ++callbacks;
	                })
	          .status == cl::Status::NotAdmitted);
	CHECK(client.admitDescriptor(af::Codec::header, 1) == cl::Status::Ok);
	std::array<unsigned, 4> order{};
	auto read = client.read(af::Field{}, [&](const auto& result) noexcept {
		CHECK(result.hasValue());
		CHECK(result.value() == 99);
		order[callbacks++] = 1;
	});
	auto write = client.write(af::Field{}, value, [&](const auto& result) noexcept {
		CHECK(result);
		order[callbacks++] = 2;
	});
	auto command = client.command(af::Command{}, value, [&](const auto& result) noexcept {
		CHECK(result.executed());
		order[callbacks++] = 3;
	});
	auto service = client.service(af::Service{}, value, [&](const auto& result) noexcept {
		CHECK(result.value() == 100);
		order[callbacks++] = 4;
	});
	CHECK(read && write && command && service);
	value = 1000;
	CHECK(client.pending() == 4);
	CHECK(client
	          .command(af::EmptyCommand{},
	                   [&](const auto&) noexcept {
		                   ++callbacks;
	                   })
	          .status == cl::Status::QueueFull);
	for (unsigned i = 0; i < 4; ++i) {
		client.process(10);
		CHECK(backend.starts == i + 1);
		CHECK(callbacks == i);
		if (i != 0) {
			unsigned snapshot;
			std::memcpy(&snapshot, backend.snapshot.data(), 4);
			CHECK(snapshot == 7);
		}
		backend.finish(i == 3 ? 100 : 99);
		client.process(10);
		CHECK(callbacks == i + 1);
	}
	CHECK((order == std::array<unsigned, 4>{1, 2, 3, 4}));
	CHECK(!client.cancel(read.handle));
	CHECK(!client.cancel(write.handle));
	CHECK(client
	          .write(af::Field{42, 1}, value,
	                 [](const auto&) noexcept {
	                 })
	          .status == cl::Status::ReadOnly);
	client.close();
	CHECK(!client.drained());
	client.process(10);
	CHECK(client.drained());
	CHECK(backend.closeCalls == 1);
	CHECK(client
	          .read(af::Field{},
	                [](const auto&) noexcept {
	                })
	          .status == cl::Status::Closed);
}

static void cancellationAndTime()
{
	af::now = UINT32_MAX - 3;
	af::Backend backend;
	Client client{backend, af::clock};
	CHECK(client.admitDescriptor(af::Codec::header, 1) == cl::Status::Ok);
	unsigned callbacks = 0;
	auto first = client.read(af::Field{}, [&](const auto& r) noexcept {
		CHECK(r.status == cl::Status::Cancelled);
		++callbacks;
	});
	CHECK(client.cancel(first.handle));
	client.process(af::now);
	CHECK(callbacks == 1);
	CHECK(backend.starts == 0);
	auto next = client.read(
	    af::Field{},
	    [&](const auto& r) noexcept {
		    CHECK(r.status == cl::Status::Timeout);
		    ++callbacks;
	    },
	    8);
	CHECK(next);
	CHECK(!client.cancel(first.handle));
	af::now = 3;
	client.process(af::now);
	CHECK(backend.starts == 1);
	CHECK(callbacks == 1);
	af::now = 4;
	client.process(af::now);
	CHECK(callbacks == 2);
	af::now = 200; // enqueue uses the clock, not the last process timestamp (4).
	CHECK(client.read(
	    af::Field{},
	    [&](const auto& r) noexcept {
		    CHECK(r.status == cl::Status::Timeout);
		    ++callbacks;
	    },
	    5));
	af::now = 205;
	client.process(af::now);
	CHECK(callbacks == 3);
	CHECK(backend.starts == 1);
	CHECK(client
	          .read(
	              af::Field{},
	              [](const auto&) noexcept {
	              },
	              0)
	          .status == cl::Status::InvalidArgument);
	CHECK(client
	          .read(
	              af::Field{},
	              [](const auto&) noexcept {
	              },
	              UINT32_MAX)
	          .status == cl::Status::InvalidArgument);
	client.close();
	client.process(af::now);
	CHECK(client.drained());
}

struct Lifetime {
	bool* executing;
	unsigned* destroyed;

	Lifetime(bool& e, unsigned& d) noexcept : executing(&e), destroyed(&d)
	{}

	Lifetime(const Lifetime&) = delete;

	Lifetime(Lifetime&& other) noexcept
	    : executing(std::exchange(other.executing, nullptr)), destroyed(other.destroyed)
	{}

	~Lifetime() noexcept
	{
		if (executing) {
			CHECK(!*executing);
			++*destroyed;
		}
	}
};

static void reentrant()
{
	af::now = 0;
	af::Backend backend;
	Client client{backend, af::clock};
	CHECK(client.admitDescriptor(af::Codec::header, 1) == cl::Status::Ok);
	bool executing = false;
	unsigned destroyed = 0, callbacks = 0;
	auto first = client.read(
	    af::Field{}, [&, lifetime = Lifetime(executing, destroyed)](const auto& r) noexcept {
		    executing = true;
		    CHECK(r);
		    ++callbacks;
		    CHECK(client.command(af::EmptyCommand{}, [&](const auto&) noexcept {
			    ++callbacks;
		    }));
		    client.process(0); // Must not recurse/start another operation.
		    CHECK(backend.starts == 1);
		    CHECK(destroyed == 0);
		    CHECK(client.pending() == 2);
		    executing = false;
	    });
	CHECK(first);
	client.process(0);
	backend.finish();
	client.process(0);
	CHECK(callbacks == 1);
	CHECK(destroyed == 1);
	CHECK(backend.starts == 1);
	client.process(0);
	CHECK(backend.starts == 2);
	backend.finish();
	client.process(0);
	CHECK(callbacks == 2);
	CHECK(client.read(af::Field{}, [&](const auto& r) noexcept {
		CHECK(r.status == cl::Status::Cancelled);
		++callbacks;
		client.close();
	}));
	CHECK(client.read(af::Field{}, [&](const auto& r) noexcept {
		CHECK(r.status == cl::Status::Cancelled);
		++callbacks;
	}));
	client.close();
	client.process(0);
	CHECK(callbacks == 3);
	CHECK(!client.drained());
	client.process(0);
	CHECK(callbacks == 4);
	CHECK(client.drained());
}

static void boundedPoolAndSchema()
{
	af::Backend backend;
	cl::DeviceClient<af::Codec, 8, 4> client{backend, af::clock};
	auto bad = af::Codec::header;
	bad[63] = std::byte{1};
	CHECK(client.admitDescriptor(bad, 1) == cl::Status::NotAdmitted);
	CHECK(client.admitDescriptor(af::Codec::header, 1) == cl::Status::Ok);
	unsigned callbacks = 0, value = 7;
	auto one = client.write(af::Field{}, value, [&](const auto&) noexcept {
		++callbacks;
	});
	CHECK(one);
	CHECK(client
	          .write(af::Field{}, value,
	                 [&](const auto&) noexcept {
		                 ++callbacks;
	                 })
	          .status == cl::Status::PayloadFull);
	CHECK(client.command(af::EmptyCommand{}, [&](const auto&) noexcept {
		++callbacks;
	}));
	CHECK(client.cancel(one.handle));
	client.process(af::now);
	client.process(af::now);
	CHECK(callbacks == 1);
	backend.finish();
	client.process(af::now);
	CHECK(callbacks == 2);
	client.close();
	client.process(af::now);
	CHECK(client.drained());
}

struct GateBackend : af::Backend {
	unsigned prepares = 0;

	cl::Packet nextPacket() noexcept override
	{
		return active ? cl::Packet{cl::Input{response}.first(1), generation, starts} : cl::Packet{};
	}

	bool prepareHandoff(std::uint32_t epoch, std::uint32_t requestId) noexcept override
	{
		++prepares;
		return active && epoch == generation && requestId == starts;
	}

	bool onReply(cl::Input, std::uint32_t, std::uint32_t) noexcept override
	{
		if (!active)
			return false;
		finish();
		return true;
	}
};

static void transportGates()
{
	af::now = 0;
	GateBackend backend;
	Client client{backend, af::clock};
	CHECK(client.admitDescriptor(af::Codec::header, 1) == cl::Status::Ok);
	unsigned callbacks = 0;
	CHECK(client.command(
	    af::Command{}, 1U,
	    [&](const auto& r) noexcept {
		    CHECK(r.status == cl::Status::Timeout);
		    ++callbacks;
	    },
	    2));
	const auto packet = client.nextPacket();
	CHECK(packet);
	af::now = 2;
	CHECK(!client.prepareHandoff(packet.session, packet.requestId));
	CHECK(backend.prepares == 0);
	CHECK(callbacks == 1);
	CHECK(client.read(
	    af::Field{},
	    [&](const auto& r) noexcept {
		    CHECK(r.status == cl::Status::Timeout);
		    ++callbacks;
	    },
	    2));
	CHECK(client.nextPacket());
	af::now = 4;
	CHECK(!client.onReply({}, 1, 2));
	CHECK(callbacks == 2);
	CHECK(client.command(af::Command{}, 1U, [&](const auto& r) noexcept {
		CHECK(r.status == cl::Status::Cancelled);
		++callbacks;
	}));
	const auto active = client.nextPacket();
	CHECK(active);
	CHECK(client.read(
	    af::Field{},
	    [&](const auto& r) noexcept {
		    CHECK(r.status == cl::Status::Timeout);
		    ++callbacks;
		    CHECK(client.cancel(client.activeHandle()));
		    CHECK(!client.prepareHandoff(active.session, active.requestId));
		    CHECK(!client.nextPacket());
		    client.close();
	    },
	    1));
	af::now = 5;
	client.process(af::now);
	CHECK(callbacks == 3);
	CHECK(backend.prepares == 0);
	client.process(af::now);
	CHECK(callbacks == 4);
	CHECK(client.drained());
}

#ifndef ASYNC_NO_THREADS
static void mpsc()
{
	af::now = 0;
	af::Backend backend;
	backend.autoComplete = true;
	using Locked =
	    cl::DeviceClient<af::Codec, 32, 128, 64, 64, alignof(std::max_align_t), std::mutex>;
	Locked owner{backend, af::clock};
	CHECK(owner.admitDescriptor(af::Codec::header, 1) == cl::Status::Ok);
	std::atomic<unsigned> wakes{0}, completed{0}, accepted{0}, producersDone{0}, wrongThread{0};
	const auto ownerId = std::this_thread::get_id();
	cl::ThreadSafeClient facade{owner,
	                            +[](void* p) noexcept {
		                            ++*static_cast<std::atomic<unsigned>*>(p);
	                            },
	                            &wakes};
	std::array<std::thread, 4> producers;
	for (auto& producer : producers)
		producer = std::thread([&] {
			for (unsigned i = 0; i < 100;) {
				auto r = facade.command(af::Command{}, i, [&](const auto&) noexcept {
					if (std::this_thread::get_id() != ownerId)
						++wrongThread;
					++completed;
				});
				if (r) {
					++accepted;
					++i;
					if ((i & 3) == 0)
						facade.cancel(r.handle);
				} else if (r.status != cl::Status::QueueFull && r.status != cl::Status::PayloadFull)
					std::abort();
				else
					std::this_thread::yield();
			}
			++producersDone;
		});
	while (producersDone.load() != 4 || owner.pending())
		owner.process(0);
	for (auto& producer : producers)
		producer.join();
	CHECK(accepted == 400);
	CHECK(completed == accepted);
	CHECK(wrongThread == 0);
	CHECK(wakes >= accepted);
	// close while a producer owns an unpublished record: owner must not wait,
	// invoke its unaccepted callback, or declare the storage drained prematurely.
	af::Codec::pauseEncode = true;
	af::Codec::enteredEncode = false;
	cl::Status submission = cl::Status::Ok;
	std::thread delayed([&] {
		submission = facade
		                 .command(af::Command{}, 1U,
		                          [&](const auto&) noexcept {
			                          ++completed;
		                          })
		                 .status;
	});
	while (!af::Codec::enteredEncode.load())
		std::this_thread::yield();
	facade.close();
	owner.process(0);
	CHECK(!owner.drained());
	const auto closeWakes = wakes.load();
	af::Codec::pauseEncode = false;
	delayed.join();
	CHECK(submission == cl::Status::Closed);
	CHECK(completed == accepted);
	CHECK(wakes.load() > closeWakes);
	owner.process(0);
	CHECK(owner.drained());
	CHECK(backend.closeCalls == 1);
}

struct RejectingCodec : af::Codec {
	template<class T>
	static std::uint8_t encode(const T& value, cl::Output bytes) noexcept
	{
		(void)af::Codec::encode(value, bytes);
		return 1;
	}
};

static void failedEncodingCloseWake()
{
	af::now = 0;
	af::Backend backend;
	using Locked =
	    cl::DeviceClient<RejectingCodec, 1, 4, 64, 64, alignof(std::max_align_t), std::mutex>;
	Locked owner{backend, af::clock};
	CHECK(owner.admitDescriptor(af::Codec::header, 1) == cl::Status::Ok);
	std::atomic<unsigned> wakes{0}, callbacks{0};
	cl::ThreadSafeClient facade{owner,
	                            +[](void* p) noexcept {
		                            ++*static_cast<std::atomic<unsigned>*>(p);
	                            },
	                            &wakes};
	af::Codec::pauseEncode = true;
	af::Codec::enteredEncode = false;
	cl::Status submission = cl::Status::Ok;
	std::thread delayed([&] {
		submission = facade
		                 .command(af::Command{}, 1U,
		                          [&](const auto&) noexcept {
			                          ++callbacks;
		                          })
		                 .status;
	});
	while (!af::Codec::enteredEncode.load())
		std::this_thread::yield();
	facade.close();
	owner.process(0);
	CHECK(!owner.drained());
	const auto closeWakes = wakes.load();
	af::Codec::pauseEncode = false;
	delayed.join();
	CHECK(submission == cl::Status::CodecError);
	CHECK(callbacks == 0);
	CHECK(wakes.load() > closeWakes);
	owner.process(0);
	CHECK(owner.drained());
	CHECK(backend.closeCalls == 1);
}
#endif

int main()
{
	functionReferences();
	nullCallbacks();
	lifecycle();
	cancellationAndTime();
	reentrant();
	boundedPoolAndSchema();
	transportGates();
#ifndef ASYNC_NO_THREADS
	mpsc();
	failedEncodingCloseWake();
#endif
	std::printf("async_core checks=%u failures=%u\n", checks, failures);
	return failures ? 1 : 0;
}
