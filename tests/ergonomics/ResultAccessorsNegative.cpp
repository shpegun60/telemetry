/*
 * @file ResultAccessorsNegative.cpp
 * @brief Owning temporary pointers are rejected for every public pointer accessor.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */

#include <telemetry/result/FieldReadResult.hpp>
#include <telemetry/result/NativeCallResult.hpp>
#include <telemetry/result/ServiceResult.hpp>

namespace ts = telemetry;

struct Payload {
	int value;
};

using Service = ts::ServiceResult<Payload>;
using Native = ts::NativeCallResult<Payload>;
using Field = ts::FieldReadResult<Payload>;

template<class Result>
Result makeResult()
{
	return Result::successFrom([]() noexcept -> Payload {
		return {7};
	});
}

// A const class prvalue selects const&&; it must not fall back to const&.
template<class Result>
const Result makeConstantResult()
{
	return makeResult<Result>();
}

#if defined(RUN)
#if CASE < 1 || CASE > 8
#error RUN reproduction requires a pointer escape case from 1 through 8
#endif
const Payload* escaped = nullptr;
#define CHECK_POINTER(expression) escaped = (expression)
#else
#define CHECK_POINTER(expression) (void)(expression)
#endif

void reject()
{
#if CASE == 1
	CHECK_POINTER(makeResult<Service>().valueOrNull());
#elif CASE == 2
	CHECK_POINTER(makeConstantResult<Service>().valueOrNull());
#elif CASE == 3
	CHECK_POINTER(makeResult<Native>().valueOrNull());
#elif CASE == 4
	CHECK_POINTER(makeConstantResult<Native>().valueOrNull());
#elif CASE == 5
	CHECK_POINTER(makeResult<Field>().valueOrNull());
#elif CASE == 6
	CHECK_POINTER(makeConstantResult<Field>().valueOrNull());
#elif CASE == 7
	CHECK_POINTER(makeResult<Field>().operator->());
#elif CASE == 8
	CHECK_POINTER(makeConstantResult<Field>().operator->());
#elif CASE == 9
	(void)makeResult<Field>()->value;
#elif CASE == 10
	(void)makeConstantResult<Field>()->value;
#else
#error Unknown intended rejection case
#endif

#if defined(RUN)
	// On the old headers, the factory temporary ended at CHECK_POINTER's semicolon.
	// Reading after that full expression exposes the escaped pointer under ASan.
	volatile int observed = escaped->value;
	(void)observed;
#endif
}

#if defined(RUN)
int main()
{
	reject();
}
#endif
