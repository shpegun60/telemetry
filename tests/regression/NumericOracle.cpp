// Independent numeric oracle: extended precision, explicit truncation and
// deterministic source bit patterns. Production conversion uses neither.
#include <telemetry/detail/NumberConversion.hpp>
#include <optional>

#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <tuple>
#include <type_traits>

namespace {
using namespace telemetry;
int checks = 0;
int failures = 0;

#if LDBL_MANT_DIG >= 64
using Numbers = std::tuple<float, double, std::uint8_t, std::uint16_t, std::uint32_t,
    std::uint64_t, std::int8_t, std::int16_t, std::int32_t, std::int64_t, bool>;

template <class T>
bool equal(T a, T b)
{
    if constexpr (std::is_floating_point_v<T>) {
        return (std::isnan(a) && std::isnan(b))
            || (a == b && (a != 0 || std::signbit(a) == std::signbit(b)));
    } else return a == b;
}

template <class To, class From>
std::optional<To> reference(From number)
{
    // A >=64-bit mantissa represents every supported integer exactly. Unlike
    // the library's source-precision bounds, this oracle truncates first.
    long double value = static_cast<long double>(number);
    if constexpr (std::is_same_v<To, bool>) {
        if (!std::isfinite(value)) return std::nullopt;
        return value != 0;
    } else if constexpr (std::is_integral_v<To>) {
        if (!std::isfinite(value)) return std::nullopt;
        value = std::trunc(value);
        if (value < static_cast<long double>(std::numeric_limits<To>::lowest())
            || value > static_cast<long double>(std::numeric_limits<To>::max())) return std::nullopt;
        return static_cast<To>(value);
    } else {
        if (std::isnan(value)) return std::numeric_limits<To>::quiet_NaN();
        if (std::isinf(value)) return std::signbit(value)
            ? -std::numeric_limits<To>::infinity() : std::numeric_limits<To>::infinity();
        if (std::fabs(value) > static_cast<long double>(std::numeric_limits<To>::max())) return std::nullopt;
        return static_cast<To>(value);
    }
}

template <class To, class From>
bool checkValue(From input, const std::optional<To>& expected)
{
    To output = static_cast<To>(7);
    const auto before = output;
    const bool ok = detail::convertNumberTo(input, output);
    ++checks;
    return ok == expected.has_value() && (ok ? equal(output, *expected) : equal(output, before));
}

std::uint64_t nextBits(std::uint64_t& state)
{
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    return state;
}

template <class T>
T sample(std::uint64_t bits)
{
    if constexpr (std::is_floating_point_v<T>) {
        T value;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    } else if constexpr (std::is_same_v<T, bool>) return (bits & 1) != 0;
    else if constexpr (std::is_signed_v<T>) {
        const auto magnitude = static_cast<T>(bits & static_cast<std::uint64_t>(std::numeric_limits<T>::max()));
        return (bits >> 63) != 0 ? static_cast<T>(-magnitude) : magnitude;
    } else return static_cast<T>(bits);
}

template <class From, class To>
void checkPair()
{
    bool correct = true;
    const auto check = [&](From number) {
        correct = checkValue<To, From>(number, reference<To>(number)) && correct;
    };
    check(From{0});
    check(From{1});
    check(std::numeric_limits<From>::lowest());
    check(std::numeric_limits<From>::max());
    if constexpr (std::is_floating_point_v<From>) {
        check(-From{0});
        check(-From{0.75});
        check(std::numeric_limits<From>::denorm_min());
        check(std::numeric_limits<From>::infinity());
        check(-std::numeric_limits<From>::infinity());
        check(std::numeric_limits<From>::quiet_NaN());
        const long double endpoints[] = {static_cast<long double>(std::numeric_limits<To>::lowest()),
                                         static_cast<long double>(std::numeric_limits<To>::max())};
        for (long double limit : endpoints) {
            if (std::fabs(limit) > static_cast<long double>(std::numeric_limits<From>::max())) continue;
            const From endpoint = static_cast<From>(limit);
            check(endpoint);
            check(std::nextafter(endpoint, -std::numeric_limits<From>::infinity()));
            check(std::nextafter(endpoint, std::numeric_limits<From>::infinity()));
        }
    }
    std::uint64_t state = 0x7e968ac20315bd4fULL;
    for (int i = 0; i < 1024; ++i) check(sample<From>(nextBits(state)));
    if constexpr (std::is_integral_v<From> && sizeof(From) <= 2) {
        // Cover every input for small integers, including transitions where
        // signedness or target width changes. The wider loop counter cannot
        // wrap at U16/S16 max, and the independent oracle stays exact.
        for (std::int64_t value = std::numeric_limits<From>::lowest();
             value <= std::numeric_limits<From>::max(); ++value) {
            check(static_cast<From>(value));
        }
    }
    if (!correct) ++failures;

}

template <class From, std::size_t... I>
void checkTargets(std::index_sequence<I...>) { (checkPair<From, std::tuple_element_t<I, Numbers>>(), ...); }

template <std::size_t... I>
void checkSources(std::index_sequence<I...> indices) { (checkTargets<std::tuple_element_t<I, Numbers>>(indices), ...); }
#endif
} // namespace

int main()
{
#if LDBL_MANT_DIG >= 64
    checkSources(std::make_index_sequence<std::tuple_size_v<Numbers>>{});
#else
    std::printf("SKIP  numeric oracle needs long double with at least 64 mantissa bits\n");
#endif
    std::printf("CHECKS %d\n", checks);
    return failures == 0 ? 0 : 1;
}
