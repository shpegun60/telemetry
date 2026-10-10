/*
 * @file ResultAccessors.cpp
 * @brief Owning result accessor categories and unchanged borrowed payload lifetimes.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */

#include <telemetry/result/BorrowedServiceResult.hpp>
#include <telemetry/result/FieldReadResult.hpp>
#include <telemetry/result/NativeCallResult.hpp>
#include <telemetry/result/ServiceCallResult.hpp>
#include <telemetry/result/ServiceResult.hpp>
#include <concepts>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <type_traits>
#include <utility>

namespace ts = telemetry;

namespace {
unsigned checks = 0;

void check(bool condition)
{
	++checks;
	if (!condition)
		std::abort();
}

// Records ownership transfers without requiring a wire type or an endpoint.
// Public methods: Payload() constructs; copy/move count transfers; ~Payload() ends lifetime.
struct Payload {
	static inline unsigned live = 0;
	static inline unsigned copies = 0;
	static inline unsigned moves = 0;
	int value;

	explicit Payload(int next = 7) noexcept : value(next)
	{
		++live;
	}

	Payload(const Payload& other) noexcept : value(other.value)
	{
		++live;
		++copies;
	}

	Payload(Payload&& other) noexcept : value(other.value)
	{
		++live;
		++moves;
		other.value = -1;
	}

	~Payload() noexcept
	{
		--live;
	}
};

template<class Ref>
concept HasPointer = requires(Ref&& result) { std::forward<Ref>(result).valueOrNull(); };

template<class Ref>
concept HasValue = requires(Ref&& result) { std::forward<Ref>(result).value(); };

template<class Ref>
concept HasArrow = requires(Ref&& result) { std::forward<Ref>(result).operator->(); };

template<class Ref>
concept HasDereference = requires(Ref&& result) { *std::forward<Ref>(result); };

// The same categories must be enforced even for nested owning result wrappers.
template<class Result, class Value>
consteval bool owningPointerMatrix()
{
	static_assert(HasPointer<Result&> && HasPointer<const Result&>);
	static_assert(!HasPointer<Result&&> && !HasPointer<const Result&&>);
	static_assert(!HasPointer<volatile Result&> && !HasPointer<const volatile Result&>);
	static_assert(!HasPointer<volatile Result&&> && !HasPointer<const volatile Result&&>);
	static_assert(std::same_as<decltype(std::declval<Result&>().valueOrNull()), Value*>);
	static_assert(
	    std::same_as<decltype(std::declval<const Result&>().valueOrNull()), const Value*>);
	static_assert(noexcept(std::declval<Result&>().valueOrNull()));
	static_assert(noexcept(std::declval<const Result&>().valueOrNull()));
	return true;
}

// Preserve the existing const-rvalue distinction rather than inventing overloads.
template<class Result, class Value, class ConstRvalue>
consteval bool owningValueMatrix()
{
	static_assert(HasValue<Result&> && HasValue<const Result&>);
	static_assert(HasValue<Result&&> && HasValue<const Result&&>);
	static_assert(!HasValue<volatile Result&> && !HasValue<const volatile Result&>);
	static_assert(!HasValue<volatile Result&&> && !HasValue<const volatile Result&&>);
	static_assert(std::same_as<decltype(std::declval<Result&>().value()), Value&>);
	static_assert(std::same_as<decltype(std::declval<const Result&>().value()), const Value&>);
	static_assert(std::same_as<decltype(std::declval<Result&&>().value()), Value&&>);
	static_assert(std::same_as<decltype(std::declval<const Result&&>().value()), ConstRvalue>);
	return true;
}

template<class Result>
consteval bool absentDereferenceMatrix()
{
	static_assert(!HasArrow<Result&> && !HasArrow<const Result&>);
	static_assert(!HasArrow<Result&&> && !HasArrow<const Result&&>);
	static_assert(!HasArrow<volatile Result&> && !HasArrow<const volatile Result&>);
	static_assert(!HasArrow<volatile Result&&> && !HasArrow<const volatile Result&&>);
	static_assert(!HasDereference<Result&> && !HasDereference<const Result&>);
	static_assert(!HasDereference<Result&&> && !HasDereference<const Result&&>);
	static_assert(!HasDereference<volatile Result&> && !HasDereference<const volatile Result&>);
	static_assert(!HasDereference<volatile Result&&> && !HasDereference<const volatile Result&&>);
	return true;
}

template<class Result, class Value>
consteval bool borrowedMatrix()
{
	static_assert(HasPointer<Result&> && HasPointer<const Result&>);
	static_assert(HasPointer<Result&&> && HasPointer<const Result&&>);
	static_assert(HasValue<Result&> && HasValue<const Result&>);
	static_assert(HasValue<Result&&> && HasValue<const Result&&>);
	static_assert(!HasPointer<volatile Result&> && !HasPointer<const volatile Result&>);
	static_assert(!HasPointer<volatile Result&&> && !HasPointer<const volatile Result&&>);
	static_assert(!HasValue<volatile Result&> && !HasValue<const volatile Result&>);
	static_assert(!HasValue<volatile Result&&> && !HasValue<const volatile Result&&>);
	static_assert(std::same_as<decltype(std::declval<Result&>().valueOrNull()), const Value*>);
	static_assert(
	    std::same_as<decltype(std::declval<const Result&>().valueOrNull()), const Value*>);
	static_assert(std::same_as<decltype(std::declval<Result&&>().valueOrNull()), const Value*>);
	static_assert(
	    std::same_as<decltype(std::declval<const Result&&>().valueOrNull()), const Value*>);
	static_assert(std::same_as<decltype(std::declval<Result&>().value()), const Value&>);
	static_assert(std::same_as<decltype(std::declval<const Result&>().value()), const Value&>);
	static_assert(std::same_as<decltype(std::declval<Result&&>().value()), const Value&>);
	static_assert(std::same_as<decltype(std::declval<const Result&&>().value()), const Value&>);
	return true;
}

template<class Result, class Value>
consteval bool borrowedDereferenceMatrix()
{
	static_assert(HasArrow<Result&> && HasArrow<const Result&>);
	static_assert(HasArrow<Result&&> && HasArrow<const Result&&>);
	static_assert(!HasArrow<volatile Result&> && !HasArrow<const volatile Result&>);
	static_assert(!HasArrow<volatile Result&&> && !HasArrow<const volatile Result&&>);
	static_assert(HasDereference<Result&> && HasDereference<const Result&>);
	static_assert(HasDereference<Result&&> && HasDereference<const Result&&>);
	static_assert(!HasDereference<volatile Result&> && !HasDereference<const volatile Result&>);
	static_assert(!HasDereference<volatile Result&&> && !HasDereference<const volatile Result&&>);
	static_assert(std::same_as<decltype(std::declval<Result&>().operator->()), const Value*>);
	static_assert(std::same_as<decltype(std::declval<const Result&>().operator->()), const Value*>);
	static_assert(std::same_as<decltype(std::declval<Result&&>().operator->()), const Value*>);
	static_assert(
	    std::same_as<decltype(std::declval<const Result&&>().operator->()), const Value*>);
	static_assert(std::same_as<decltype(*std::declval<Result&>()), const Value&>);
	static_assert(std::same_as<decltype(*std::declval<const Result&>()), const Value&>);
	static_assert(std::same_as<decltype(*std::declval<Result&&>()), const Value&>);
	static_assert(std::same_as<decltype(*std::declval<const Result&&>()), const Value&>);
	return true;
}

using Service = ts::ServiceResult<Payload>;
using Native = ts::NativeCallResult<Payload>;
using Field = ts::FieldReadResult<Payload>;
using Borrowed = ts::BorrowedServiceResult<Payload>;
using BorrowedField = ts::BorrowedValue<Payload>;
using Nested = ts::NativeCallResult<Borrowed>;

static_assert(owningPointerMatrix<Service, Payload>());
static_assert(owningPointerMatrix<Native, Payload>());
static_assert(owningPointerMatrix<Field, Payload>());
static_assert(owningPointerMatrix<Nested, Borrowed>());
static_assert(owningValueMatrix<Service, Payload, const Payload&>());
static_assert(owningValueMatrix<Native, Payload, const Payload&&>());
static_assert(owningValueMatrix<Field, Payload, const Payload&>());
static_assert(owningValueMatrix<Nested, Borrowed, const Borrowed&&>());
static_assert(absentDereferenceMatrix<Service>());
static_assert(absentDereferenceMatrix<Native>());
static_assert(borrowedMatrix<Borrowed, Payload>());
static_assert(borrowedMatrix<BorrowedField, Payload>());
static_assert(borrowedMatrix<ts::BorrowedServiceCallResult<Payload>, Payload>());
static_assert(borrowedDereferenceMatrix<Borrowed, Payload>());
static_assert(borrowedDereferenceMatrix<BorrowedField, Payload>());
static_assert(absentDereferenceMatrix<ts::BorrowedServiceCallResult<Payload>>());

static_assert(HasArrow<Field&> && HasArrow<const Field&>);
static_assert(!HasArrow<Field&&> && !HasArrow<const Field&&>);
static_assert(!HasArrow<volatile Field&> && !HasArrow<const volatile Field&>);
static_assert(!HasArrow<volatile Field&&> && !HasArrow<const volatile Field&&>);
static_assert(std::same_as<decltype(std::declval<Field&>().operator->()), Payload*>);
static_assert(std::same_as<decltype(std::declval<const Field&>().operator->()), const Payload*>);
static_assert(noexcept(std::declval<Field&>().operator->()));
static_assert(noexcept(std::declval<const Field&>().operator->()));
static_assert(HasDereference<Field&> && HasDereference<const Field&>);
static_assert(HasDereference<Field&&> && HasDereference<const Field&&>);
static_assert(!HasDereference<volatile Field&> && !HasDereference<const volatile Field&>);
static_assert(!HasDereference<volatile Field&&> && !HasDereference<const volatile Field&&>);
static_assert(std::same_as<decltype(*std::declval<Field&>()), Payload&>);
static_assert(std::same_as<decltype(*std::declval<const Field&>()), const Payload&>);
static_assert(std::same_as<decltype(*std::declval<Field&&>()), Payload&&>);
static_assert(std::same_as<decltype(*std::declval<const Field&&>()), const Payload&>);
static_assert(!HasPointer<ts::ServiceResult<void>&> && !HasPointer<const ts::ServiceResult<void>&>);
static_assert(!HasPointer<ts::ServiceResult<void>&&> &&
              !HasPointer<const ts::ServiceResult<void>&&>);

template<class Result>
Result makeOwned(int value)
{
	return Result::successFrom([value]() noexcept -> Payload {
		return Payload{value};
	});
}

template<class Result, class Status>
void owningRuntime(Status failure)
{
	const unsigned initialLive = Payload::live;
	const unsigned initialCopies = Payload::copies;
	const unsigned initialMoves = Payload::moves;
	{
		auto result = makeOwned<Result>(11);
		const auto constant = makeOwned<Result>(13);
		check(result.hasValue() && constant.hasValue());
		check(result.valueOrNull() == std::addressof(result.value()));
		check(constant.valueOrNull() == std::addressof(constant.value()));
		check(Payload::live == initialLive + 2);
		check(Payload::copies == initialCopies && Payload::moves == initialMoves);
		result.valueOrNull()->value = 17;
		check(result.value().value == 17);
		check(constant.valueOrNull()->value == 13);

		auto rejected = Result::failure(failure);
		const auto constantRejected = Result::failure(failure);
		check(!rejected.hasValue() && rejected.valueOrNull() == nullptr);
		check(!constantRejected.hasValue() && constantRejected.valueOrNull() == nullptr);
		check(Payload::live == initialLive + 2);

		Payload extracted = std::move(result).value();
		check(extracted.value == 17 && result.value().value == -1);
		check(Payload::moves == initialMoves + 1 && Payload::copies == initialCopies);
		check(result.hasValue() && result.valueOrNull() != nullptr);
		check(Payload::live == initialLive + 3);
	}
	check(Payload::live == initialLive);
	Payload extracted = makeOwned<Result>(19).value();
	check(extracted.value == 19 && Payload::live == initialLive + 1);
}

} // namespace

int main()
{
	owningRuntime<Service>(ts::ServiceStatus::Busy);
	check(Payload::live == 0);
	owningRuntime<Native>(ts::NativeCallStatus::NotFound);
	check(Payload::live == 0);
	owningRuntime<Field>(ts::FieldReadStatus::ConversionFailed);
	check(Payload::live == 0);

	{
		auto field = makeOwned<Field>(23);
		const auto constant = makeOwned<Field>(29);
		check(field.operator->() == field.valueOrNull());
		check(constant.operator->() == constant.valueOrNull());
		field->value = 31;
		check((*field).value == 31 && constant->value == 29);
		auto rejected = Field::failure(ts::FieldReadStatus::NotFound);
		const auto constantRejected = Field::failure(ts::FieldReadStatus::Unavailable);
		check(rejected.operator->() == nullptr && constantRejected.operator->() == nullptr);
		check(makeOwned<Field>(37).value().value == 37);
		check((*makeOwned<Field>(41)).value == 41);
		check(static_cast<const Field&&>(makeOwned<Field>(43)).value().value == 43);
		check((*static_cast<const Field&&>(makeOwned<Field>(47))).value == 47);
	}
	check(Payload::live == 0);

	{
		Payload backing{53};
		const Payload constantBacking{59};
		const auto initialCopies = Payload::copies;
		const auto initialMoves = Payload::moves;
		const Payload* fieldPointer = BorrowedField::from(backing).valueOrNull();
		const Payload* constantFieldPointer =
		    static_cast<const BorrowedField&&>(BorrowedField::from(constantBacking)).valueOrNull();
		const Payload* servicePointer = Borrowed::success(backing).valueOrNull();
		const Payload* constantServicePointer =
		    static_cast<const Borrowed&&>(Borrowed::success(constantBacking)).valueOrNull();
		check(fieldPointer == &backing && servicePointer == &backing);
		check(constantFieldPointer == &constantBacking &&
		      constantServicePointer == &constantBacking);
		check(fieldPointer->value == 53 && constantServicePointer->value == 59);
		check(BorrowedField{}.valueOrNull() == nullptr);
		check(static_cast<const BorrowedField&&>(BorrowedField{}).valueOrNull() == nullptr);
		check(Borrowed::failure(ts::ServiceStatus::Busy).valueOrNull() == nullptr);
		check(static_cast<const Borrowed&&>(Borrowed::failure(ts::ServiceStatus::Failed))
		          .valueOrNull() == nullptr);
		backing.value = 61;
		check(servicePointer->value == 61); // Temporary views do not freeze application state.

		auto nested = Nested::successFrom([&]() noexcept {
			return Borrowed::success(backing);
		});
		const auto constantNested = Nested::successFrom([&]() noexcept {
			return Borrowed::success(constantBacking);
		});
		check(nested.valueOrNull() == &nested.value());
		check(constantNested.valueOrNull() == &constantNested.value());
		check(nested.valueOrNull()->valueOrNull() == &backing);
		check(constantNested.value().valueOrNull() == &constantBacking);
		const Payload* innerPointer = Nested::successFrom([&]() noexcept {
			                              return Borrowed::success(backing);
		                              })
		                                  .value()
		                                  .valueOrNull();
		check(innerPointer == &backing && innerPointer->value == 61);

		using FlatBorrowed = ts::BorrowedServiceCallResult<Payload>;
		const Payload* flatPointer = FlatBorrowed::fromNative([&]() noexcept {
			                             return Nested::successFrom([&]() noexcept {
				                             return Borrowed::success(backing);
			                             });
		                             }).valueOrNull();
		const Payload* constantFlatPointer =
		    static_cast<const FlatBorrowed&&>(FlatBorrowed::fromNative([&]() noexcept {
			    return Nested::successFrom([&]() noexcept {
				    return Borrowed::success(constantBacking);
			    });
		    })).valueOrNull();
		check(flatPointer == &backing && constantFlatPointer == &constantBacking);
		check(FlatBorrowed::fromNative([]() noexcept {
			      return Nested::failure(ts::NativeCallStatus::NotFound);
		      }).valueOrNull() == nullptr);
		check(static_cast<const FlatBorrowed&&>(FlatBorrowed::fromNative([]() noexcept {
			      return Nested::failure(ts::NativeCallStatus::SignatureMismatch);
		      })).valueOrNull() == nullptr);
		check(Payload::copies == initialCopies && Payload::moves == initialMoves);
		check(Payload::live == 2);
	}
	check(Payload::live == 0);
	std::printf("Result accessors: %u checks\n", checks);
}
