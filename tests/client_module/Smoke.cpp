/* Isolated client/common dependency and compatibility checks.
 * Authors: Ruslan Kovtun (shpegun60), codexAi. MIT. */
#include <common/ContextFunctionSlot.hpp>
#include <common/DelegateRefSlot.hpp>
#include <common/DelegateSlot.hpp>
#include <common/FunctionSlot.hpp>
#include <common/OwnerSlot.hpp>
#include <client/ThreadSafeClient.hpp>
#include "../async_client/Fixture.hpp"
#include <cstdio>

namespace af = async_fixture;
namespace cl = telemetry::client;
static unsigned checks = 0, failures = 0;

static void check(bool condition)
{
	++checks;
	if (!condition)
		++failures;
}

int main()
{
	af::Backend backend;
	cl::DeviceClient<af::Codec, 3, 16> client{backend, af::clock};
	check(client.admitDescriptor(af::Codec::header, 1) == cl::Status::Ok);
	unsigned calls = 0;
	std::array<unsigned, 3> order{};
	const auto first =
	    client.read(af::Field{}, [&](const cl::Result<std::uint32_t>& result) noexcept {
		    check(result.succeeded() && result.hasValue());
		    check(result.value() == 71);
		    order[calls++] = 1;
	    });
	std::uint32_t input = 37;
	const auto second =
	    client.write(af::Field{}, input, [&](const cl::Result<void>& result) noexcept {
		    check(result.succeeded());
		    order[calls++] = 2;
	    });
	const auto cancelled =
	    client.read(af::Field{}, [&](const cl::Result<std::uint32_t>& result) noexcept {
		    check(result.status == cl::Status::Cancelled && !result.hasValue());
		    order[calls++] = 3;
	    });
	check(first && second && cancelled);
	input = 99;
	check(client.pending() == 3 && calls == 0);
	client.process(0);
	check(backend.starts == 1 && calls == 0);
	backend.finish(71);
	client.process(0);
	check(calls == 1 && backend.starts == 1);
	client.process(0);
	check(backend.starts == 2);
	std::uint32_t snapshot = 0;
	std::memcpy(&snapshot, backend.snapshot.data(), sizeof(snapshot));
	check(snapshot == 37);
	backend.finish();
	client.process(0);
	check(client.cancel(cancelled.handle));
	client.process(0);
	check(calls == 3 && client.pending() == 0);
	check((order == std::array<unsigned, 3>{1, 2, 3}));
	check(!client.cancel(first.handle));
	client.close();
	check(!client.drained());
	client.process(0);
	check(client.drained() && backend.closeCalls == 1);

	telemetry::DelegateSlot<void(unsigned&) noexcept> owned;
	owned.bind([](unsigned& value) noexcept {
		++value;
	});
	unsigned slotCalls = 0;
	owned.invoke(slotCalls);
	check(slotCalls == 1);
	auto borrowed = [&](unsigned& value) noexcept {
		value += slotCalls;
	};
	telemetry::DelegateRefSlot<void(unsigned&) noexcept> reference;
	reference.bind(borrowed);
	unsigned value = 2;
	reference.invoke(value);
	check(value == 3);
	std::printf("client module checks=%u failures=%u\n", checks, failures);
	return failures ? 1 : 0;
}
