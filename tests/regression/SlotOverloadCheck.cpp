// A slot invokes the specialization selected for its declared signature.
// Authors: Ruslan Kovtun (shpegun60), codexAi. License: MIT.
// Checks reference-preserving callback selection in the presence of plausible value-taking overloads.
// Default arguments, ellipses and object qualifiers expose selection rules that simple signatures miss.

#include <telemetry/Telemetry.hpp>
#include <cstdio>

using namespace telemetry;

namespace {
unsigned checks = 0;

// Defaulted second argument makes a copied-value overload appear callable.
// Public methods:
// - operator(): Offer competing forms.
struct DefaultedValue {
	template<class U>
	void operator()(U& value) const noexcept
	{
		value = 1;
	}

	void operator()(int, int = 0) const noexcept
	{}
};

// Ellipsis makes a copied-value overload appear callable.
// Public methods:
// - operator(): Offer competing forms.
struct EllipsisValue {
	template<class U>
	void operator()(U& value) const noexcept
	{
		value = 1;
	}

	void operator()(int, ...) const noexcept
	{}
};

// Volatile-qualified copied-value form competes with the reference callback.
// Public methods:
// - operator(): Offer competing forms.
struct VolatileValue {
	template<class U>
	void operator()(U& value) const noexcept
	{
		value = 1;
	}

	void operator()(int) volatile noexcept
	{}
};

// Mutable reference form competes with a const copied-value form.
// Public methods:
// - operator(): Offer competing forms.
struct MutableReference {
	template<class U>
	void operator()(U& value) noexcept
	{
		value = 1;
	}

	void operator()(int) const noexcept
	{}
};

template<class F>
bool check()
{
	F callable{};
	DelegateSlot<void(int&) noexcept> owned;
	DelegateRefSlot<void(int&) noexcept> borrowed;
	int value = 0;
	owned.bind(callable);
	owned.invoke(value);
	++checks;
	if (value != 1)
		return false;
	value = 0;
	borrowed.bind(callable);
	borrowed.invoke(value);
	++checks;
	return value == 1;
}
} // namespace

int main()
{
	const bool results[]{check<DefaultedValue>(), check<EllipsisValue>(), check<VolatileValue>(),
	                     check<MutableReference>()};
	bool ok = true;
	for (const bool result : results)
		ok = result && ok;
	std::printf("CHECKS %u\n", checks);
	return ok ? 0 : 1;
}
