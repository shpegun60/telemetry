// Maintained owner-local lifetime reentry checks (MIT).
#ifndef TELEMETRY_TESTS_DELEGATE_REENTRYCHECKS_HPP
#define TELEMETRY_TESTS_DELEGATE_REENTRYCHECKS_HPP
#pragma once

namespace delegate_assignment_checks {
struct ReentryState {
	unsigned calls = 0, copies = 0, moves = 0, live = 0, released = 0, conversions = 0;
};

// Counted unique ownership without allocating in the heap-disabled gate.
struct ReentryResource {
	ReentryState* state;
	bool owned = true;

	explicit ReentryResource(ReentryState& value) noexcept : state(&value)
	{
		++state->live;
	}

	ReentryResource(const ReentryResource& other) noexcept : state(other.state), owned(other.owned)
	{
		++state->copies;
		if (owned)
			++state->live;
	}

	ReentryResource(ReentryResource&& other) noexcept
	    : state(other.state), owned(std::exchange(other.owned, false))
	{
		++state->moves;
	}

	~ReentryResource() noexcept
	{
		if (owned) {
			--state->live;
			++state->released;
		}
	}

	int operator()() noexcept
	{
		return 71;
	}
};
enum class ReentryAction {
	Reset,
	Lvalue,
	Rvalue,
	Function,
	Conversion,
	MoveAssign,
	MoveFrom,
	Borrow
};

struct ReentryConversion {
	ReentryState* state;

	operator Function() const noexcept
	{
		++state->conversions;
		return &functionValue;
	}
};

template<class D>
struct ReentryContext {
	D* owner;
	ReentryState* state;
	ReentryAction action;

	void run() noexcept
	{
		++state->calls;
		DELEGATE_CHECK(!*owner && !owner->uses_inline() && !owner->uses_heap());
		if constexpr (std::is_same_v<D, tiny::delegate<int(), 64>>)
			DELEGATE_CHECK(!owner->owning() && !owner->non_owning());
		owner->reset();
		DELEGATE_CHECK(!*owner);
		switch (action) {
			case ReentryAction::Reset:
				*owner = nullptr;
				break;
			case ReentryAction::Lvalue:
			case ReentryAction::Rvalue: {
				ReentryResource candidate{*state};
				const auto copies = state->copies, moves = state->moves;
				if (action == ReentryAction::Lvalue)
					*owner = candidate;
				else
					*owner = std::move(candidate);
				DELEGATE_CHECK(candidate.owned);
				DELEGATE_CHECK(state->copies == copies && state->moves == moves);
				break;
			}
			case ReentryAction::Function:
				*owner = &functionValue;
				break;
			case ReentryAction::Conversion:
				*owner = ReentryConversion{state};
				DELEGATE_CHECK(state->conversions == 0);
				break;
			case ReentryAction::MoveAssign: {
				D candidate{ReentryResource{*state}};
				const auto moves = state->moves;
				*owner = std::move(candidate);
				DELEGATE_CHECK(candidate && candidate() == 71);
				DELEGATE_CHECK(state->moves == moves);
				break;
			}
			case ReentryAction::MoveFrom: {
				D moved{std::move(*owner)};
				DELEGATE_CHECK(!moved && !*owner);
				D assigned{&functionValue};
				assigned = std::move(*owner);
				DELEGATE_CHECK(!assigned && !*owner);
				// Neither attempt may clear the source owner's transition marker.
				*owner = &functionValue;
				break;
			}
			case ReentryAction::Borrow: {
				ReentryResource candidate{*state};
				if constexpr (std::is_assignable_v<D&, decltype(tiny::borrow(candidate))>)
					*owner = tiny::borrow(candidate);
				DELEGATE_CHECK(candidate.owned);
				break;
			}
		}
		DELEGATE_CHECK(!*owner && !owner->uses_inline() && !owner->uses_heap());
		if constexpr (std::is_same_v<D, tiny::delegate<int(), 64>>)
			DELEGATE_CHECK(!owner->owning() && !owner->non_owning());
	}
};

template<class D, std::size_t Padding>
struct ReentryDestructor {
	unsigned char padding[Padding]{};
	ReentryContext<D>* context;

	explicit ReentryDestructor(ReentryContext<D>& value) noexcept : context(&value)
	{}

	ReentryDestructor(ReentryDestructor&& other) noexcept
	    : context(std::exchange(other.context, nullptr))
	{}

	~ReentryDestructor() noexcept
	{
		if (context)
			context->run();
	}

	int operator()() noexcept
	{
		return 71;
	}
};

template<class D, std::size_t Padding>
void destructionReentry()
{
	for (auto action :
	     {ReentryAction::Reset, ReentryAction::Lvalue, ReentryAction::Rvalue,
	      ReentryAction::Function, ReentryAction::Conversion, ReentryAction::MoveAssign,
	      ReentryAction::MoveFrom, ReentryAction::Borrow}) {
		for (unsigned finish = 0; finish != 5; ++finish) {
			ReentryState state;
			ReentryContext<D> context{nullptr, &state, action};
			{
				D owner;
				context.owner = &owner;
				owner = ReentryDestructor<D, Padding>{context};
				DELEGATE_CHECK(owner && owner() == 71 && state.calls == 0);
				DELEGATE_CHECK(owner.uses_heap() == (sizeof(ReentryDestructor<D, Padding>) >
				                                     D::inline_capacity_bytes()));
				if (finish == 0)
					owner.reset();
				else if (finish == 1)
					owner = &functionValue;
				else if (finish == 2)
					owner = ReentryResource{state};
				else if (finish == 3) {
					D replacement{ReentryResource{state}};
					owner = std::move(replacement);
					DELEGATE_CHECK(!replacement);
				}
				if (finish != 4) {
					DELEGATE_CHECK(state.calls == 1);
					DELEGATE_CHECK(finish == 0 ? !owner : owner() == 71);
				}
				// finish=4 invokes the same checks from the owner's destructor.
			}
			DELEGATE_CHECK(state.calls == 1 && state.live == 0);
		}
	}
}

// A moved-from target can refer to its source owner or the actual destination.
template<class D>
struct ReentryMovedFrom {
	ReentryContext<D>* context;
	bool fire = false;

	explicit ReentryMovedFrom(ReentryContext<D>& value) noexcept : context(&value)
	{}

	ReentryMovedFrom(const ReentryMovedFrom& other) noexcept : context(other.context)
	{}

	ReentryMovedFrom(ReentryMovedFrom&& other) noexcept : context(other.context)
	{
		other.fire = true;
	}

	~ReentryMovedFrom() noexcept
	{
		if (fire)
			context->run();
	}

	int operator()() noexcept
	{
		return 71;
	}
};

template<class D>
struct ReentryConstructor {
	ReentryContext<D>* context;

	explicit ReentryConstructor(ReentryContext<D>& value) noexcept : context(&value)
	{}

	ReentryConstructor(ReentryConstructor&& other) noexcept : context(other.context)
	{
		context->run();
	}

	int operator()() noexcept
	{
		return 71;
	}
};

template<class D>
void constructionReentry()
{
	ReentryState state;
	ReentryContext<D> context{nullptr, &state, ReentryAction::Function};
	{
		D source;
		context.owner = &source;
		ReentryMovedFrom<D> target{context};
		source = target; // The actual destination, not just its staging temporary.
		DELEGATE_CHECK(state.calls == 1 && source() == 71);
		D moved{std::move(source)};
		DELEGATE_CHECK(state.calls == 2 && !source && moved() == 71);
		alignas(D) unsigned char storage[sizeof(D)];
		context.owner = reinterpret_cast<D*>(storage);
		D* destination = ::new (storage) D{std::move(moved)};
		DELEGATE_CHECK(state.calls == 3 && !moved && (*destination)() == 71);
		destination->~D();
	}
	DELEGATE_CHECK(state.live == 0);
	{
		alignas(D) unsigned char storage[sizeof(D)];
		context.owner = reinterpret_cast<D*>(storage);
		ReentryConstructor<D> target{context};
		D* constructed = ::new (storage) D{std::move(target)};
		DELEGATE_CHECK(state.calls == 4 && (*constructed)() == 71);
		constructed->~D();
	}
	DELEGATE_CHECK(state.live == 0);
}
} // namespace delegate_assignment_checks
#endif
