// Nullable class-specific allocation preserves owning-assignment targets (MIT).
static unsigned nullAllocationAssertions = 0;
#define TINY_DELEGATE_ENABLE_HEAP_FALLBACK 1
#define TINY_DELEGATE_ASSERT(expr, msg) ((expr) ? (void)0 : (void)++nullAllocationAssertions)
#include <delegate/tiny_delegate.hpp>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <utility>

namespace {
unsigned checks = 0;

void check(bool condition, int line)
{
	++checks;
	if (!condition) {
		std::fprintf(stderr, "CHECK failed at %d\n", line);
		std::abort();
	}
}

#define CHECK(expr) check(bool(expr), __LINE__)

struct NullAllocation {
	static inline unsigned allocations = 0;
	static inline unsigned copies = 0;
	static inline unsigned moves = 0;
	char padding[128]{};

	static void* operator new(std::size_t) noexcept
	{
		++allocations;
		return nullptr;
	}

	static void operator delete(void*) noexcept
	{}

	NullAllocation() noexcept = default;

	NullAllocation(const NullAllocation&) noexcept
	{
		++copies;
	}

	NullAllocation(NullAllocation&&) noexcept
	{
		++moves;
	}

	int operator()() noexcept
	{
		return 91;
	}
};

template<unsigned Padding>
struct PreviousTarget {
	unsigned* destroyed;
	char padding[Padding]{};

	explicit PreviousTarget(unsigned& count) noexcept : destroyed(&count)
	{}

	PreviousTarget(PreviousTarget&& other) noexcept : destroyed(other.destroyed)
	{
		other.destroyed = nullptr;
	}

	~PreviousTarget() noexcept
	{
		if (destroyed)
			++*destroyed;
	}

	int operator()() noexcept
	{
		return 19;
	}
};

struct FalseFunctor {
	explicit operator bool() const noexcept
	{
		return false;
	}

	int operator()() noexcept
	{
		return 27;
	}
};

template<class D, unsigned Padding>
void preserveTarget()
{
	unsigned destroyed = 0;
	D target{PreviousTarget<Padding>{destroyed}};
	CHECK(target && target() == 19);
	CHECK(target.uses_heap() == (sizeof(PreviousTarget<Padding>) > D::inline_capacity_bytes()));
	NullAllocation source;
	for (bool move : {false, true}) {
		const auto beforeAllocation = NullAllocation::allocations;
		const auto beforeAssertion = nullAllocationAssertions;
		if (move)
			target = std::move(source);
		else
			target = source;
		CHECK(NullAllocation::allocations == beforeAllocation + 1);
		CHECK(nullAllocationAssertions == beforeAssertion + 1);
		CHECK(NullAllocation::copies == 0 && NullAllocation::moves == 0);
		CHECK(target);
		CHECK(target() == 19);
		CHECK(destroyed == 0);
		CHECK(target.uses_heap() == (sizeof(PreviousTarget<Padding>) > D::inline_capacity_bytes()));
	}
	target.reset();
	CHECK(!target && destroyed == 1);
}

template<class D>
void family()
{
	NullAllocation source;
	const auto beforeAssertion = nullAllocationAssertions;
	const auto beforeAllocation = NullAllocation::allocations;
	D failed{source};
	CHECK(!failed);
	CHECK(!failed.uses_heap() && !failed.uses_inline());
	CHECK(NullAllocation::allocations == beforeAllocation + 1);
	CHECK(nullAllocationAssertions == beforeAssertion + 1);
	CHECK(NullAllocation::copies == 0 && NullAllocation::moves == 0);
	preserveTarget<D, 8>();
	preserveTarget<D, 128>();
	D target{FalseFunctor{}};
	CHECK(target && target() == 27);
	target = FalseFunctor{};
	CHECK(target && target() == 27);
	D empty;
	target = std::move(empty);
	CHECK(!target && !empty); // Empty delegate move assignment must still clear.
}
} // namespace

int main()
{
	family<tiny::delegate<int(), 64>>();
	family<tiny::delegate_sbo<int(), 64>>();
	CHECK(NullAllocation::allocations == 10 && nullAllocationAssertions == 10);
	std::printf("CHECKS %u\n", checks);
}
