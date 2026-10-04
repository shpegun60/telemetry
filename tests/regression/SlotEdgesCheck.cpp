// Regression promoted from tests/review/slots/SlotEdges.cpp.
// Authors: Ruslan Kovtun (shpegun60), codexAi. License: MIT.
// Intended for ASan/UBSan runs; every check here is defined behavior.
// Checks slot capacity, alignment, closure replacement and reentrant borrowed rebinding.
// Every runtime case uses defined behavior and is suitable for the runner's sanitizer configurations.

#include <telemetry/Telemetry.hpp>
#include <cstdint>
#include <cstdio>
#include <memory>
using namespace telemetry;

namespace {
int checks = 0, failures = 0;

void expect(bool ok, const char* label)
{
	++checks;
	if (!ok) {
		++failures;
		std::printf("FAIL %s\n", label);
	}
}

// Over-aligned captured value used to check owned closure placement.
struct alignas(64) Wide {
	float value;
};

int live = 0;

// Closure whose independent live count exposes replacement and destruction.
// Public methods:
// - Counted(): Count live copies.
// - ~Counted(): Reduce live count.
// - operator(): Return stored value.
struct Counted {
	float value;

	explicit Counted(float v) noexcept : value(v)
	{
		++live;
	}

	Counted(const Counted& o) noexcept : value(o.value)
	{
		++live;
	}

	Counted(Counted&& o) noexcept : value(o.value)
	{
		++live;
	}

	~Counted() noexcept
	{
		--live;
	}

	float operator()() noexcept
	{
		return value;
	}
};

// Exactly the default 32-byte capacity on every host/target.
// Closure occupying exactly the default inline byte capacity.
// Public methods:
// - operator(): Return final byte.
struct Exact32 {
	unsigned char bytes[32];

	float operator()() noexcept
	{
		return float(bytes[31]);
	}
};

static_assert(sizeof(Exact32) == 32);

DelegateRefSlot<float() noexcept> refSelf;
ContextFunctionSlot<float() noexcept> contextSelf;

float second() noexcept
{
	return 2.f;
}

float secondContext(void*) noexcept
{
	return 2.f;
}

// Borrowed callback that replaces its own slot during an active invocation.
// Public methods:
// - operator(): Replace borrowed target.
struct RebindingFunctor {
	float operator()() noexcept
	{
		refSelf.bind(&second);
		return 1.f;
	}
} rebinding;

float firstContext(void*) noexcept
{
	contextSelf.bind(&secondContext, nullptr);
	return 1.f;
}

void incrementContext(void* context, int& value) noexcept
{
	value += *static_cast<int*>(context);
}

void takeContext(void*, std::unique_ptr<int>&& value) noexcept
{
	std::unique_ptr<int> owned = std::move(value);
}
} // namespace

int main()
{
	{
		// A raised alignment must place the owned closure on that boundary.
		DelegateSlot<float() noexcept, 128, 64> aligned;
		Wide wide{5.f};
		aligned.bind([wide]() noexcept {
			const auto address = reinterpret_cast<std::uintptr_t>(&wide);
			return address % 64 == 0 ? wide.value : -1.f;
		});
		expect(aligned.invoke() == 5.f, "over-aligned capture stored on its 64-byte boundary");
		expect(alignof(decltype(aligned)) == 64 && sizeof(aligned) % 64 == 0,
		       "slot alignment follows Align");
	}
	{
		DelegateSlot<float() noexcept> exact;
		Exact32 target{};
		target.bytes[31] = 9;
		exact.bind(target);
		expect(exact.invoke() == 9.f, "closure of exactly Bytes is accepted and copied");
	}
	{
		// Replacement and slot destruction each destroy exactly the owned copy.
		{
			DelegateSlot<float() noexcept> local;
			local.bind(Counted{1.f});
			expect(live == 1, "one owned copy after binding a temporary");
			local.bind(Counted{2.f});
			expect(live == 1 && local.invoke() == 2.f, "replacement destroyed the previous copy");
			local.bind(nullptr);
			expect(live == 0, "bind(nullptr) destroyed the owned copy");
			local.bind(Counted{3.f});
		}
		expect(live == 0, "slot destruction destroyed the owned copy");
	}
	{
		// Non-owning kinds may rebind themselves from inside the running call.
		refSelf.bind(rebinding);
		expect(refSelf.invoke() == 1.f && refSelf.invoke() == 2.f,
		       "DelegateRefSlot self-rebind during a call");
		contextSelf.bind(&firstContext, nullptr);
		expect(contextSelf.invoke() == 1.f && contextSelf.invoke() == 2.f,
		       "ContextFunctionSlot self-rebind during a call");
	}
	{
		// Reference and rvalue-reference parameters are forwarded, not copied.
		ContextFunctionSlot<void(int&) noexcept> increment;
		int step = 3, value = 4;
		increment.bind(&incrementContext, &step);
		increment.invoke(value);
		expect(value == 7, "ContextFunctionSlot forwards an lvalue reference");
		ContextFunctionSlot<void(std::unique_ptr<int>&&) noexcept> take;
		take.bind(&takeContext, nullptr);
		auto owned = std::make_unique<int>(1);
		take.invoke(std::move(owned));
		expect(!owned, "ContextFunctionSlot forwards an rvalue reference");
		DelegateSlot<void(int&) noexcept> ownedIncrement;
		ownedIncrement.bind([&step](int& v) noexcept {
			v += step;
		});
		ownedIncrement.invoke(value);
		expect(value == 10, "DelegateSlot forwards an lvalue reference");
		DelegateRefSlot<void(std::unique_ptr<int>&&) noexcept> refTake;
		auto consume = [](std::unique_ptr<int>&& p) noexcept {
			std::unique_ptr<int> sink = std::move(p);
		};
		refTake.bind(consume);
		auto other = std::make_unique<int>(2);
		refTake.invoke(std::move(other));
		expect(!other, "DelegateRefSlot forwards an rvalue reference");
	}
	std::printf("CHECKS %d\n", checks);
	return failures ? 1 : 0;
}
