/* Compile-time client misuse gates with a matching valid control. MIT. */
#include "Fixture.hpp"

#ifndef ASYNC_NEGATIVE_CASE
#define ASYNC_NEGATIVE_CASE 0
#endif

namespace af = async_fixture;
namespace cl = telemetry::client;

struct ThrowingCopy {
	ThrowingCopy() noexcept = default;

	ThrowingCopy(const ThrowingCopy&) noexcept(false)
	{}

	ThrowingCopy(ThrowingCopy&&) noexcept = default;

	void operator()(const cl::ResultView&) noexcept
	{}
};

static_assert(!std::is_nothrow_constructible_v<ThrowingCopy, ThrowingCopy&>);
static_assert(std::is_nothrow_move_constructible_v<ThrowingCopy>);

struct TooLargeCallback {
	std::array<std::byte, 256> storage{};

	void operator()(const cl::ResultView&) noexcept
	{}
};

struct TooLargeField {
	static constexpr unsigned category = 1;
	using Value = std::array<std::byte, 128>;
	std::uint32_t id = 42;

	bool readable() const noexcept
	{
		return true;
	}
};

struct MoveOnly {
	MoveOnly() noexcept = default;
	MoveOnly(const MoveOnly&) = delete;
	MoveOnly(MoveOnly&&) noexcept = default;

	void operator()(const cl::ResultView&) noexcept
	{}
};

int main()
{
	af::Backend backend;
	cl::DeviceClient<af::Codec> client{backend, af::clock};
#if ASYNC_NEGATIVE_CASE == 0
	// Positive control: the same owned shape is valid from a nothrow rvalue.
	(void)client.read(af::Field{}, ThrowingCopy{});
	(void)client.read(af::Field{}, MoveOnly{});
	(void)client.submitEncoded({cl::Operation::FieldRead, 42, {}, 4}, MoveOnly{});
#elif ASYNC_NEGATIVE_CASE == 1
	// Wrapping F in a new lambda must not hide its throwing source copy.
	ThrowingCopy callback;
	(void)client.read(af::Field{}, callback);
#elif ASYNC_NEGATIVE_CASE == 2
	ThrowingCopy callback;
	(void)client.submitEncoded({cl::Operation::FieldRead, 42, {}, 4}, callback);
#elif ASYNC_NEGATIVE_CASE == 3
	(void)client.read(af::Field{}, TooLargeCallback{});
#elif ASYNC_NEGATIVE_CASE == 4
	(void)client.read(TooLargeField{}, [](const auto&) noexcept {
	});
#elif ASYNC_NEGATIVE_CASE == 5
	(void)client.read(af::Command{}, [](const auto&) noexcept {
	});
#elif ASYNC_NEGATIVE_CASE == 6
	cl::ThreadSafeClient facade{client}; // Default NoLock is not a real MPSC policy.
	(void)facade;
#elif ASYNC_NEGATIVE_CASE == 7
	(void)client.read(af::Field{}, [](const auto&) {
	}); // Invocation may throw.
#else
#error Unknown ASYNC_NEGATIVE_CASE
#endif
}
