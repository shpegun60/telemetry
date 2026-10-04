/* Native view lifetime/identity, immutable access and status transitions. MIT. */
#include "Fixture.hpp"
#include "Check.hpp"
#include <concepts>
#include <utility>

using namespace borrowed_fixture;
using borrowed_test::check;

namespace {
// Noncopyable referent counts live objects to prove result views never own their payload.
// API: Counted()/~Counted() observe lifetime; copy and assignment are deleted.
struct Counted {
	inline static unsigned live = 0;
	int value = 7;

	Counted() noexcept
	{
		++live;
	}

	Counted(const Counted&) = delete;
	Counted& operator=(const Counted&) = delete;

	~Counted()
	{
		--live;
	}
};

using View = ts::BorrowedValue<Counted>;
using Result = ts::BorrowedServiceResult<Counted>;
static_assert(std::same_as<View::value_type, Counted>);
static_assert(std::same_as<decltype(std::declval<View>().value()), const Counted&>);
static_assert(std::same_as<decltype(*std::declval<View>()), const Counted&>);
static_assert(std::same_as<decltype(std::declval<View>().operator->()), const Counted*>);
static_assert(std::same_as<decltype(std::declval<Result>().value()), const Counted&>);
static_assert(std::is_trivially_copyable_v<View> && std::is_standard_layout_v<View>);
static_assert(std::is_trivially_copyable_v<Result> && std::is_standard_layout_v<Result>);
static_assert(sizeof(View) == sizeof(Counted*) && alignof(View) == alignof(Counted*));
static_assert(sizeof(Result) == 2 * sizeof(Counted*) && alignof(Result) == alignof(Counted*));
static_assert(sizeof(ts::BorrowedValue<Blob<65536>>) == sizeof(View));
static_assert(sizeof(ts::BorrowedServiceResult<Blob<65536>>) == sizeof(Result));
} // namespace

int main(int argc, char** argv)
{
	if (argc == 2 && argv[1][0] == 'o') {
		(void)Result::failure(SS::Ok);
		return 0; // The checked failure factory must not return.
	}
	if (argc == 2 && argv[1][0] == 'i') {
		(void)Result::failure(static_cast<SS>(255));
		return 0;
	}
	borrowed_test::start();
#ifndef BORROWED_ARM
	if (argc == 2 && argv[1][0] == 'a') {
		auto* observed = ::operator new(16);
		::operator delete(observed);
	}
#endif
	{
		Counted object;
		const Counted constant;
		View empty;
		check(!empty && !empty.hasValue() && empty.valueOrNull() == nullptr);
		auto view = View::from(object);
		check(view && view.valueOrNull() == std::addressof(object));
		auto constView = View::from(constant);
		check(constView.valueOrNull() == std::addressof(constant));
		check(std::addressof(view.value()) == std::addressof(object) &&
		      view->value == (*view).value);
		auto copied = view;
		check(copied.valueOrNull() == view.valueOrNull());
		auto moved = std::move(copied);
		check(moved.valueOrNull() == std::addressof(object));
		empty = view;
		check(empty.valueOrNull() == std::addressof(object));
		empty = std::move(moved);
		check(empty.valueOrNull() == std::addressof(object));
		empty = View{};
		check(!empty && Counted::live == 2);
		object.value = 11;
		check(view.value().value == 11); // A view is not a frozen snapshot.
		auto result = Result::success(object);
		check(result.status() == SS::Ok && result.hasValue() && bool(result));
		check(result.valueOrNull() == std::addressof(object) && result->value == 11);
		auto resultCopy = result;
		check(resultCopy.valueOrNull() == std::addressof(object));
		auto resultMove = std::move(resultCopy);
		check(resultMove.valueOrNull() == std::addressof(object));
		auto failure = Result::failure(SS::Busy);
		failure = result;
		check(failure.status() == SS::Ok && failure.valueOrNull() == std::addressof(object));
		failure = std::move(resultMove);
		check(failure.status() == SS::Ok && std::addressof(*failure) == std::addressof(object));
		for (auto status : {SS::InvalidArgument, SS::Unavailable, SS::Busy, SS::Failed}) {
			const auto rejected = Result::failure(status);
			check(rejected.status() == status && !rejected && !rejected.hasValue() &&
			      rejected.valueOrNull() == nullptr);
		}
		auto failureCopy = Result::failure(SS::Unavailable);
		auto another = failureCopy;
		check(another.status() == SS::Unavailable && another.valueOrNull() == nullptr);
		check(Counted::live == 2); // Deleted payload copies prove ownership is absent.
	}
	check(Counted::live == 0);
	return borrowed_test::finish(); // 24 conditions, including allocation observation.
}
