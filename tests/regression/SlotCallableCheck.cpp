// Ordinary overload selection and generic argument deduction in delegate slots.
// Authors: Ruslan Kovtun (shpegun60), codexAi. License: MIT.
#include "Telemetry.h"
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <type_traits>
#include <utility>

using namespace telemetry;
namespace {
unsigned checks = 0;
#define CHECK(...) do { ++checks; if (!(__VA_ARGS__)) { \
    std::fprintf(stderr, "line %d: %s\n", __LINE__, #__VA_ARGS__); std::abort(); } } while (false)

void increment(int& value) noexcept { ++value; }
template <class T> struct Identity { using Type = T; };
struct NonDeducible {
    template <class T = int>
    void operator()(typename Identity<T>::Type& value) const noexcept { value += 2; }
};
struct ConcreteReference {
    unsigned* calls;
    template <class T> void operator()(T) const noexcept { *calls += 100; }
    void operator()(int& value) const noexcept { ++*calls; value += 3; }
};
struct ConcreteValue {
    template <class T> void operator()(T&) const noexcept {}
    void operator()(int) const noexcept {}
};
struct CvReference {
    unsigned* calls;
    void operator()(int& value) & noexcept { ++*calls; value += 4; }
    void operator()(int& value) const & noexcept { ++*calls; value += 5; }
};
struct CopyPreferred {
    void operator()(int) noexcept {}
    void operator()(int&) const noexcept {}
};
}

int main()
{
    int value = 0;
    unsigned calls = 0;
    DelegateSlot<void(int&) noexcept> owned;
    DelegateRefSlot<void(int&) noexcept> borrowed;

    // The body is ill-formed for an rvalue; only the actual int& deduction is valid.
    auto forward = [](auto&& x) noexcept { increment(std::forward<decltype(x)>(x)); };
    owned.bind(forward);
    owned.invoke(value);
    CHECK(value == 1);
    borrowed.bind(forward);
    borrowed.invoke(value);
    CHECK(value == 2);

    auto constReference = [&calls, &value](const auto& x) noexcept {
        static_assert(std::is_const_v<std::remove_reference_t<decltype(x)>>);
        CHECK(&x == &value);
        ++calls;
    };
    owned.bind(constReference);
    owned.invoke(value);
    CHECK(calls == 1 && value == 2);
    borrowed.bind(constReference);
    borrowed.invoke(value);
    CHECK(calls == 2 && value == 2);

    ConcreteReference exact{&calls};
    owned.bind(exact);
    owned.invoke(value);
    CHECK(calls == 3 && value == 5);
    borrowed.bind(exact);
    borrowed.invoke(value);
    CHECK(calls == 4 && value == 8);
    static_assert(!detail::slotSignatureMatches<ConcreteValue, void, int&>);
    static_assert(!detail::slotSignatureMatches<CopyPreferred, void, int&>);

    CvReference cv{&calls};
    const CvReference constCv{&calls};
    owned.bind(cv);
    owned.invoke(value);
    CHECK(calls == 5 && value == 12);
    borrowed.bind(cv);
    borrowed.invoke(value);
    CHECK(calls == 6 && value == 16);
    borrowed.bind(constCv);
    borrowed.invoke(value);
    CHECK(calls == 7 && value == 21);

    DelegateSlot<void(const int&) noexcept> ownedRead;
    DelegateRefSlot<void(const int&) noexcept> borrowedRead;
    auto copyRead = [&calls](auto x) noexcept {
        static_assert(std::is_same_v<decltype(x), int>);
        ++x;
        ++calls;
    };
    ownedRead.bind(copyRead);
    ownedRead.invoke(value);
    CHECK(calls == 8 && value == 21);
    borrowedRead.bind(copyRead);
    borrowedRead.invoke(value);
    CHECK(calls == 9 && value == 21);
    ownedRead.bind(constReference);
    ownedRead.invoke(value);
    CHECK(calls == 10 && value == 21);
    borrowedRead.bind(constReference);
    borrowedRead.invoke(value);
    CHECK(calls == 11 && value == 21);

    NonDeducible defaults;
    owned.bind(defaults);
    owned.invoke(value);
    CHECK(value == 23);
    borrowed.bind(defaults);
    borrowed.invoke(value);
    CHECK(value == 25);

    auto variadic = [](auto&... xs) noexcept { (increment(xs), ...); };
    DelegateSlot<void(int&, int&) noexcept> pair;
    int other = 0;
    pair.bind(variadic);
    pair.invoke(value, other);
    CHECK(value == 26 && other == 1);
    auto mixed = [](auto& destination, auto source) noexcept { destination += int(source); };
    DelegateSlot<void(int&, float) noexcept> mixedSlot;
    mixedSlot.bind(mixed);
    mixedSlot.invoke(value, 4.f);
    CHECK(value == 30);

    auto mixedConst = [](const auto& read, auto&& write) noexcept {
        static_assert(std::is_const_v<std::remove_reference_t<decltype(read)>>);
        increment(std::forward<decltype(write)>(write));
    };
    pair.bind(mixedConst);
    pair.invoke(value, other);
    CHECK(value == 30 && other == 2);

    auto discard = [&calls](auto&& x) noexcept {
        increment(std::forward<decltype(x)>(x));
        ++calls;
        return 123;
    };
    owned.bind(discard);
    owned.invoke(value);
    CHECK(calls == 12 && value == 31);

    DelegateSlot<void(std::unique_ptr<int>&&) noexcept> consume;
    consume.bind([&calls](auto&& pointer) noexcept {
        auto retained = std::forward<decltype(pointer)>(pointer);
        CHECK(*retained == 42);
        ++calls;
    });
    auto pointer = std::make_unique<int>(42);
    consume.invoke(std::move(pointer));
    CHECK(!pointer && calls == 13);

#if __cplusplus >= 202002L
    owned.bind([]<class T>(T& x) noexcept requires std::is_integral_v<T> { increment(x); });
    owned.invoke(value);
    CHECK(value == 32);
#endif
    std::printf("%u slot callable checks passed\n", checks);
}
