// Maintained owning-assignment lifetime checks for bundled tiny_delegate (MIT).
#ifndef TELEMETRY_TESTS_DELEGATE_ASSIGNMENTCHECKS_HPP
#define TELEMETRY_TESTS_DELEGATE_ASSIGNMENTCHECKS_HPP
#pragma once

#include <delegate/tiny_delegate.hpp>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <new>
#include <utility>

namespace delegate_assignment_checks {
inline unsigned checks = 0;
inline unsigned allocations = 0;
inline unsigned deallocations = 0;
inline unsigned liveAllocations = 0;
inline bool failAllocation = false;

[[noreturn]] inline void allocationFailure()
{
#if defined(__cpp_exceptions)
	throw std::bad_alloc{};
#else
	std::abort();
#endif
}

inline void check(bool condition, int line)
{
	++checks;
	if (!condition) {
		std::fprintf(stderr, "Delegate assignment CHECK failed at %d\n", line);
		std::abort();
	}
}
} // namespace delegate_assignment_checks

#define DELEGATE_CHECK(condition) ::delegate_assignment_checks::check(bool(condition), __LINE__)
#if defined(__GNUC__) || defined(__clang__)
#define DELEGATE_ALLOC_NOINLINE __attribute__((noinline))
#else
#define DELEGATE_ALLOC_NOINLINE
#endif

DELEGATE_ALLOC_NOINLINE void* operator new(std::size_t size)
{
	using namespace delegate_assignment_checks;
	++allocations;
	if (failAllocation || !TINY_DELEGATE_ENABLE_HEAP_FALLBACK) {
		failAllocation = false;
		allocationFailure();
	}
	if (void* pointer = std::malloc(size ? size : 1)) {
		++liveAllocations;
		return pointer;
	}
	allocationFailure();
}

DELEGATE_ALLOC_NOINLINE void operator delete(void* pointer) noexcept
{
	if (!pointer)
		return;
	++delegate_assignment_checks::deallocations;
	--delegate_assignment_checks::liveAllocations;
	std::free(pointer);
}

void operator delete(void* pointer, std::size_t) noexcept
{
	::operator delete(pointer);
}

void* operator new[](std::size_t size)
{
	return ::operator new(size);
}

void operator delete[](void* pointer) noexcept
{
	::operator delete(pointer);
}

void operator delete[](void* pointer, std::size_t) noexcept
{
	::operator delete(pointer);
}

namespace delegate_assignment_checks {
struct Value {
	int value = 71;
	Value() noexcept = default;

	Value(const Value& other) noexcept : value(other.value)
	{}

	Value(Value&& other) noexcept : value(other.value)
	{}

	~Value() noexcept
	{
		value = 0;
	}

	int operator()() noexcept
	{
		return value;
	}
};

template<std::size_t Padding>
struct Outer {
	unsigned char padding[Padding]{};
	Value member{};
	Value** exposed;

	explicit Outer(Value** out) noexcept : exposed(out)
	{}

	int operator()() noexcept
	{
		*exposed = &member;
		return member();
	}
};

template<class D, std::size_t Padding>
void callableMember()
{
	for (bool moveSource : {false, true}) {
		Value* exposed = nullptr;
		D target{Outer<Padding>{&exposed}};
		DELEGATE_CHECK(target() == 71 && exposed);
		DELEGATE_CHECK(target.uses_heap() == (sizeof(Outer<Padding>) > D::inline_capacity_bytes()));
		if (moveSource)
			target = std::move(*exposed);
		else
			target = *exposed;
		DELEGATE_CHECK(target() == 71);
		DELEGATE_CHECK(target.uses_inline());
	}
}

struct Whole {
	int value = 71;
	Whole** exposed;

	explicit Whole(Whole** out) noexcept : exposed(out)
	{}

	Whole(const Whole&) noexcept = default;
	Whole(Whole&&) noexcept = default;

	~Whole() noexcept
	{
		value = 0;
	}

	int operator()() noexcept
	{
		*exposed = this;
		return value;
	}
};

struct CopyFailure {};

struct ThrowingCopy {
	// Keep exception injection opaque to allocation-elision optimizations.
	static inline volatile bool fail = false;
	int value = 71;
	ThrowingCopy() noexcept = default;

	ThrowingCopy(const ThrowingCopy& other) : value(other.value)
	{
		if (fail) {
#if defined(__cpp_exceptions)
			throw CopyFailure{};
#else
			std::abort();
#endif
		}
	}

	ThrowingCopy(ThrowingCopy&& other) noexcept : value(other.value)
	{}

	int operator()() noexcept
	{
		return value;
	}
};

inline int functionValue() noexcept
{
	return 71;
}

using Function = int (*)();

template<std::size_t Padding>
struct FunctionOuter {
	unsigned char padding[Padding]{};
	Function member = &functionValue;
	Function** exposed;

	explicit FunctionOuter(Function** out) noexcept : exposed(out)
	{}

	int operator()() noexcept
	{
		*exposed = &member;
		return member();
	}
};

struct Conversion {
	operator Function() const noexcept
	{
		return &functionValue;
	}
};

template<std::size_t Padding>
struct ConversionOuter {
	unsigned char padding[Padding]{};
	Conversion member{};
	Conversion** exposed;

	explicit ConversionOuter(Conversion** out) noexcept : exposed(out)
	{}

	int operator()() noexcept
	{
		*exposed = &member;
		return functionValue();
	}
};

template<class D, std::size_t Padding>
void functionMembers()
{
	Function* function = nullptr;
	D target{FunctionOuter<Padding>{&function}};
	DELEGATE_CHECK(target() == 71 && function);
	target = *function; // The pointer value must be saved before reset.
	DELEGATE_CHECK(target() == 71);
	Conversion* conversion = nullptr;
	target = ConversionOuter<Padding>{&conversion};
	DELEGATE_CHECK(target() == 71 && conversion);
	target = *conversion; // A conversion also completes before reset.
	DELEGATE_CHECK(target() == 71);
}

struct Counted {
	static inline unsigned copies = 0, moves = 0;
	int value = 71;
	Counted() noexcept = default;

	Counted(const Counted& other) noexcept : value(other.value)
	{
		++copies;
	}

	Counted(Counted&& other) noexcept : value(other.value)
	{
		++moves;
	}

	int operator()() noexcept
	{
		return value;
	}
};

template<class D>
void common()
{
	callableMember<D, 8>();
	Whole* whole = nullptr;
	D target{Whole{&whole}};
	DELEGATE_CHECK(target() == 71 && whole);
	target = *whole;
	DELEGATE_CHECK(target() == 71);
	DELEGATE_CHECK(whole);
	target = std::move(*whole);
	DELEGATE_CHECK(target() == 71);
	D* same = &target;
	target = std::move(*same);
	DELEGATE_CHECK(target() == 71);

	target = []() noexcept {
		return 19;
	};
	ThrowingCopy source;
#if defined(__cpp_exceptions)
	ThrowingCopy::fail = true;
	bool caught = false;
	try {
		target = source;
	} catch (const CopyFailure&) {
		caught = true;
	}
	ThrowingCopy::fail = false;
	DELEGATE_CHECK(caught);
	DELEGATE_CHECK(target() == 19);
#endif
	target = source;
	DELEGATE_CHECK(target() == 71);

	functionMembers<D, 8>();
	Counted counted;
	Counted::copies = Counted::moves = 0;
	target = counted;
	DELEGATE_CHECK(Counted::copies == 1 && Counted::moves == 1);
	DELEGATE_CHECK(target() == 71);
	D other{counted};
	Counted::copies = Counted::moves = 0;
	target = std::move(other);
	DELEGATE_CHECK(Counted::copies == 0 && Counted::moves == 2);
	DELEGATE_CHECK(target() == 71 && !other);
}

#if TINY_DELEGATE_ENABLE_HEAP_FALLBACK
template<class D>
struct Nested {
	unsigned char padding[8]{};
	D member{[]() noexcept {
		return 71;
	}};
	D** exposed;

	explicit Nested(D** out) noexcept : exposed(out)
	{}

	Nested(Nested&&) noexcept = default;

	int operator()() noexcept
	{
		*exposed = &member;
		return member();
	}
};

struct Large {
	unsigned char padding[128]{};

	int operator()() noexcept
	{
		return 71;
	}
};

struct LargeThrowingCopy {
	unsigned char padding[128]{};
	ThrowingCopy member;

	int operator()() noexcept
	{
		return member();
	}
};

struct MutableCopy {
	int value = 71;
	MutableCopy** exposed;

	explicit MutableCopy(MutableCopy** out) noexcept : exposed(out)
	{}

	MutableCopy(MutableCopy& other) noexcept : value(other.value), exposed(other.exposed)
	{}

	MutableCopy(const MutableCopy&) = delete;
	MutableCopy(MutableCopy&&) = delete;

	int operator()() noexcept
	{
		*exposed = this;
		return value;
	}
};

struct VolatileMove {
	int value = 71;
	VolatileMove** exposed;

	explicit VolatileMove(VolatileMove** out) noexcept : exposed(out)
	{}

	VolatileMove(volatile VolatileMove&& other) noexcept
	    : value(other.value), exposed(other.exposed)
	{}

	VolatileMove(const VolatileMove&) = delete;
	VolatileMove(VolatileMove&&) = delete;

	int operator()() noexcept
	{
		*exposed = this;
		return value;
	}
};

template<class D>
void heap()
{
	callableMember<D, 128>();
	functionMembers<D, 128>();
	D* member = nullptr;
	D target{Nested<D>{&member}};
	DELEGATE_CHECK(target() == 71 && member);
	DELEGATE_CHECK(target.uses_heap());
	target = std::move(*member);
	DELEGATE_CHECK(target() == 71);
	DELEGATE_CHECK(target.uses_inline());

	target = []() noexcept {
		return 19;
	};
	Large source;
#if defined(__cpp_exceptions)
	const auto beforeAllocations = allocations;
	const auto beforeLive = liveAllocations;
	failAllocation = true;
	bool caught = false;
	try {
		target = source;
	} catch (const std::bad_alloc&) {
		caught = true;
	}
	DELEGATE_CHECK(caught);
	DELEGATE_CHECK(allocations == beforeAllocations + 1);
	DELEGATE_CHECK(liveAllocations == beforeLive);
	DELEGATE_CHECK(target() == 19);
#endif
	const auto beforeSuccessAllocation = allocations;
	target = source;
	DELEGATE_CHECK(target() == 71 && target.uses_heap());
	DELEGATE_CHECK(allocations == beforeSuccessAllocation + 1);

#if defined(__cpp_exceptions)
	target = []() noexcept {
		return 19;
	};
	LargeThrowingCopy throwingSource;
	const auto beforeCopyAllocation = allocations;
	const auto beforeCopyDeallocation = deallocations;
	const auto beforeCopyLive = liveAllocations;
	ThrowingCopy::fail = true;
	caught = false;
	try {
		target = throwingSource;
	} catch (const CopyFailure&) {
		caught = true;
	}
	ThrowingCopy::fail = false;
	DELEGATE_CHECK(caught);
	DELEGATE_CHECK(allocations == beforeCopyAllocation + 1);
	DELEGATE_CHECK(deallocations == beforeCopyDeallocation + 1);
	DELEGATE_CHECK(liveAllocations == beforeCopyLive);
	DELEGATE_CHECK(target() == 19);
#endif

	MutableCopy* mutablePointer = nullptr;
	MutableCopy mutableSource{&mutablePointer};
	target = mutableSource;
	DELEGATE_CHECK(target() == 71 && mutablePointer);
	target = *mutablePointer;
	DELEGATE_CHECK(target() == 71 && target.uses_heap());
	VolatileMove* volatilePointer = nullptr;
	VolatileMove volatileSource{&volatilePointer};
	target = static_cast<volatile VolatileMove&&>(volatileSource);
	DELEGATE_CHECK(target() == 71 && volatilePointer);
	target = static_cast<volatile VolatileMove&&>(*volatilePointer);
	DELEGATE_CHECK(target() == 71 && target.uses_heap());
}
#endif
} // namespace delegate_assignment_checks

int main()
{
	using namespace delegate_assignment_checks;
	using Delegate = tiny::delegate<int(), 64>;
	using Sbo = tiny::delegate_sbo<int(), 64>;
	static_assert(std::is_nothrow_move_assignable_v<Delegate>);
	static_assert(std::is_nothrow_move_assignable_v<Sbo>);
	common<Delegate>();
	common<Sbo>();
#if TINY_DELEGATE_ENABLE_HEAP_FALLBACK
	heap<Delegate>();
	heap<Sbo>();
	DELEGATE_CHECK(allocations > 0 && deallocations > 0);
#else
	DELEGATE_CHECK(allocations == 0 && deallocations == 0);
#endif
	if (liveAllocations != 0)
		std::fprintf(stderr, "Delegate allocation counters: new=%u delete=%u live=%u\n",
		             allocations, deallocations, liveAllocations);
	DELEGATE_CHECK(liveAllocations == 0);
	std::printf("CHECKS %u\n", checks);
}

#undef DELEGATE_CHECK
#undef DELEGATE_ALLOC_NOINLINE

#endif // TELEMETRY_TESTS_DELEGATE_ASSIGNMENTCHECKS_HPP
