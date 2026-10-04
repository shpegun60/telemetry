/*
 * @file ServiceProbe.cpp
 * @brief Native Service binding, status and owning result-normalization checks.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */

#include <telemetry/service/Service.hpp>

#include <cstdint>
#include <functional>
#include <type_traits>

namespace probe {

struct Request {
	std::uint16_t id;
};

struct Response {
	std::uint32_t count;
};

struct EmptyResponse {};

using Result = telemetry::ServiceResult<Response>;
using Status = telemetry::ServiceStatus;

inline int calls = 0;

Response read(const Request& request) noexcept
{
	++calls;
	return {request.id + 10u};
}

Result readChecked(const Request& request) noexcept
{
	++calls;
	if (request.id == 0)
		return Result::failure(Status::InvalidArgument);
	if (request.id == 253)
		return Result::failure(Status::Unavailable);
	if (request.id == 254)
		return Result::failure(Status::Busy);
	if (request.id == 255)
		return Result::failure(Status::Failed);
	return Result::success({request.id + 20u});
}

Result readAlternate(const Request& request) noexcept
{
	++calls;
	return Result::success({request.id + 50u});
}

Response readByValue(Request request) noexcept
{
	++calls;
	return {request.id + 30u};
}

EmptyResponse emptyResponse() noexcept
{
	++calls;
	return {};
}

void reset() noexcept
{
	++calls;
}

telemetry::ServiceResult<void> resetChecked(const Request& request) noexcept
{
	++calls;
	if (request.id == 0)
		return telemetry::ServiceResult<void>::failure(Status::Busy);
	return telemetry::ServiceResult<void>::success();
}

// Service owner distinguishes const and mutable method bindings while counting calls.
// API: read(), readMutable().
struct Device {
	std::uint32_t base = 100;

	Response read(const Request& request) const noexcept
	{
		++calls;
		return {base + request.id};
	}

	Result readMutable(const Request& request) noexcept
	{
		++calls;
		return Result::success({base += request.id});
	}
};

struct DerivedDevice : Device {};

struct Prefix {
	std::uint32_t marker = 0x12345678u;
};

struct OffsetDerivedDevice : Prefix, Device {};

Result readContext(void* raw, const Request& request) noexcept
{
	++calls;
	auto* device = static_cast<Device*>(raw);
	return Result::success({device->base + request.id});
}

// Custom Service binding snapshots its target once before availability and invocation.
// API: snapshot(), available(), invoke().
struct SnapshotBinding {
	using Signature = Response(const Request&) noexcept;
	int* snapshots;

	int snapshot() const noexcept
	{
		return ++*snapshots;
	}

	static bool available(int) noexcept
	{
		return true;
	}

	static Response invoke(int selected, const Request&) noexcept
	{
		return {static_cast<std::uint32_t>(selected)};
	}
};

// Stateful callable operator must retain its bias even though unary plus exposes a free
// function.
// API: operator() invokes the object; operator+() exposes its alternate function.
struct FunctionConvertible {
	int bias;
	using Function = Response (*)(const Request&) noexcept;

	Function operator+() const noexcept
	{
		return &read;
	}

	Response operator()(const Request& request) const noexcept
	{
		++calls;
		return {static_cast<std::uint32_t>(bias + request.id)};
	}
};

// Empty callable distinguishes its operator result from the function exposed by unary plus.
// API: operator() invokes the object; operator+() exposes its alternate function.
struct EmptyPlus {
	using Function = Response (*)(const Request&) noexcept;

	Function operator+() const noexcept
	{
		return &read;
	}

	Response operator()(const Request&) const noexcept
	{
		return {999};
	}
};

} // namespace probe

namespace ts = telemetry;

constexpr auto fixed = ts::service<&probe::read>("Read");
static_assert(std::is_same_v<typename decltype(fixed)::Request, probe::Request>);
static_assert(std::is_same_v<typename decltype(fixed)::Response, probe::Response>);
static_assert(std::is_same_v<decltype(fixed.call(probe::Request{1})), probe::Result>);

int main()
{
	using probe::Request;
	probe::Device device{};
	const probe::Device& constantDevice = device;

	if (fixed.call(Request{2}).value().count != 12 || probe::calls != 1)
		return 1;

	auto checked = ts::service<&probe::readChecked>("Checked");
	if (checked.call(Request{0}).status() != probe::Status::InvalidArgument)
		return 2;
	if (checked.call(Request{3}).value().count != 23)
		return 3;
	if (checked.call(Request{253}).status() != probe::Status::Unavailable)
		return 23;
	if (checked.call(Request{254}).status() != probe::Status::Busy)
		return 24;
	if (checked.call(Request{255}).status() != probe::Status::Failed)
		return 25;

	auto byValue = ts::service<&probe::readByValue>("ByValue");
	if (byValue.call(Request{2}).value().count != 32)
		return 26;

	auto emptyResponse = ts::service<&probe::emptyResponse>("EmptyResponse");
	if (!emptyResponse.call().hasValue())
		return 27;

	auto noRequest = ts::service<&probe::reset>("Reset");
	if (noRequest.call().status() != probe::Status::Ok)
		return 4;

	auto wrappedVoid = ts::service<&probe::resetChecked>("ResetChecked");
	if (wrappedVoid.call(Request{0}).status() != probe::Status::Busy)
		return 28;
	if (wrappedVoid.call(Request{1}).status() != probe::Status::Ok)
		return 29;

	int snapshots = 0;
	ts::ServiceDefinition snapshotService{"Snapshot", probe::SnapshotBinding{&snapshots}};
	if (snapshotService.call(Request{0}).value().count != 1 || snapshots != 1)
		return 30;

	auto owned = ts::service<&probe::Device::read>("Owned", constantDevice);
	if (owned.call(Request{4}).value().count != 104)
		return 5;
	probe::DerivedDevice derivedDevice{};
	auto inherited = ts::service<&probe::Device::read>("Inherited", derivedDevice);
	if (inherited.call(Request{4}).value().count != 104)
		return 43;
	probe::OffsetDerivedDevice offsetDerived{};
	if (static_cast<const void*>(static_cast<probe::Device*>(&offsetDerived)) ==
	    static_cast<const void*>(&offsetDerived))
		return 44;
	auto offsetOwner = ts::service<&probe::Device::read>("OffsetOwner", offsetDerived);
	if (offsetOwner.call(Request{4}).value().count != 104)
		return 45;
	auto wrappedOwner = ts::service<&probe::Device::read>("WrappedOwner", std::cref(device));
	if (wrappedOwner.call(Request{4}).value().count != 104)
		return 22;

	auto mutableOwner = ts::service<&probe::Device::readMutable>("Mutable", device);
	if (mutableOwner.call(Request{5}).value().count != 105)
		return 6;

	auto pointer = ts::service("Pointer", &probe::read);
	if (pointer.call(Request{6}).value().count != 16)
		return 7;
	auto bareFunction = ts::service("BareFunction", probe::read);
	if (bareFunction.call(Request{6}).value().count != 16)
		return 38;
	probe::Response (*emptyPointer)(const Request&) noexcept = nullptr;
	auto noFunction = ts::service("EmptyPointer", emptyPointer);
	const int beforeEmptyPointer = probe::calls;
	if (noFunction.call(Request{6}).status() != probe::Status::Unavailable ||
	    probe::calls != beforeEmptyPointer)
		return 37;

	auto lambda = ts::service("Lambda", [](const Request& request) noexcept {
		return probe::read(request);
	});
	if (lambda.call(Request{7}).value().count != 17)
		return 8;

	auto borrowedCallable = [&device](const Request& request) noexcept {
		return device.read(request);
	};
	auto borrowed = ts::service("Borrowed", borrowedCallable);
	if (borrowed.call(Request{8}).value().count != 113)
		return 9;
	auto wrapped = ts::service("Wrapped", std::ref(borrowedCallable));
	if (wrapped.call(Request{9}).value().count != 114)
		return 10;

	probe::FunctionConvertible convertingCallable{70};
	auto retainedState = ts::service("RetainedState", convertingCallable);
	if (retainedState.call(Request{7}).value().count != 77)
		return 36;
	probe::EmptyPlus emptyPlus{};
	auto retainedOperator = ts::service("RetainedOperator", emptyPlus);
	if (retainedOperator.call(Request{7}).value().count != 999)
		return 39;

	telemetry::OwnerSlot<probe::Device> ownerSlot;
	auto selectedOwner = ts::service<&probe::Device::readMutable>("OwnerSlot", ownerSlot);
	const int beforeEmptyOwner = probe::calls;
	if (selectedOwner.call(Request{1}).status() != probe::Status::Unavailable)
		return 11;
	if (probe::calls != beforeEmptyOwner)
		return 31;
	ownerSlot.bind(device);
	if (selectedOwner.call(Request{1}).value().count != 106)
		return 12;
	probe::Device alternateDevice{200};
	ownerSlot.bind(alternateDevice);
	if (selectedOwner.call(Request{1}).value().count != 201)
		return 32;
	ownerSlot.reset();
	if (selectedOwner.call(Request{1}).status() != probe::Status::Unavailable)
		return 13;

	telemetry::FunctionSlot<probe::Result(const Request&) noexcept> functionSlot;
	auto selectedFunction = ts::service("FunctionSlot", functionSlot);
	const int beforeEmptyFunction = probe::calls;
	if (selectedFunction.call(Request{1}).status() != probe::Status::Unavailable)
		return 14;
	if (probe::calls != beforeEmptyFunction)
		return 33;
	functionSlot.bind(&probe::readChecked);
	if (selectedFunction.call(Request{1}).value().count != 21)
		return 15;
	functionSlot.bind(&probe::readAlternate);
	if (selectedFunction.call(Request{1}).value().count != 51)
		return 34;
	functionSlot.reset();
	if (selectedFunction.call(Request{1}).status() != probe::Status::Unavailable)
		return 35;

	telemetry::ContextFunctionSlot<probe::Result(const Request&) noexcept> contextSlot;
	auto selectedContext = ts::service("ContextSlot", contextSlot);
	const int beforeEmptyContext = probe::calls;
	if (selectedContext.call(Request{1}).status() != probe::Status::Unavailable)
		return 16;
	if (probe::calls != beforeEmptyContext)
		return 40;
	contextSlot.bind(&probe::readContext, &device);
	if (selectedContext.call(Request{1}).value().count != 107)
		return 17;

	telemetry::DelegateRefSlot<probe::Response(const Request&) noexcept> refSlot;
	auto selectedRef = ts::service("RefSlot", refSlot);
	const int beforeEmptyRef = probe::calls;
	if (selectedRef.call(Request{1}).status() != probe::Status::Unavailable)
		return 18;
	if (probe::calls != beforeEmptyRef)
		return 41;
	refSlot.bind(borrowedCallable);
	if (selectedRef.call(Request{1}).value().count != 107)
		return 19;

	telemetry::DelegateSlot<probe::Response(const Request&) noexcept> valueSlot;
	auto selectedValue = ts::service("ValueSlot", valueSlot);
	const int beforeEmptyValue = probe::calls;
	if (selectedValue.call(Request{1}).status() != probe::Status::Unavailable)
		return 20;
	if (probe::calls != beforeEmptyValue)
		return 42;
	valueSlot.bind([&device](const Request& request) noexcept {
		return device.read(request);
	});
	if (selectedValue.call(Request{1}).value().count != 107)
		return 21;

	return 0;
}
