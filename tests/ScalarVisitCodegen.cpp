// Compare std::visit with the library's source-tag switch using exactly the
// same alternatives. Compile separately at -O2/-Os with IndexCodegen.cpp's
// Cortex-M7 flags. Include visitor helpers and tables when comparing sizes.
#include "core/TelemetryConversion.h"

namespace {
template <class> struct ScalarVariant;
template <std::size_t... Indices>
struct ScalarVariant<std::index_sequence<Indices...>> {
    using Type = std::variant<telemetry::Scalar::NativeType<
        static_cast<telemetry::ScalarType>(Indices)>...>;
};
using Storage = ScalarVariant<std::make_index_sequence<telemetry::Scalar::typeCount>>::Type;
static_assert(sizeof(Storage) == sizeof(telemetry::Scalar));

template <class T>
TELEMETRY_FORCE_INLINE std::optional<T> visitRead(const Storage& value) noexcept
{
    return std::visit([](const auto& number) noexcept -> std::optional<T> {
        if constexpr (std::is_same_v<std::decay_t<decltype(number)>, std::monostate>) {
            return std::nullopt;
        } else {
            return telemetry::detail::readNumber<T>(number);
        }
    }, value);
}
}

extern "C" {
__attribute__((noinline)) std::optional<std::uint32_t> switch_read_u32(const telemetry::Scalar& value) noexcept
{
    return telemetry::convertScalar<std::uint32_t>(value);
}
__attribute__((noinline)) std::optional<std::uint32_t> visit_read_u32(const Storage& value) noexcept
{
    return visitRead<std::uint32_t>(value);
}
__attribute__((noinline)) float switch_known_f32(float value) noexcept
{
    return *telemetry::convertScalar<float>(telemetry::Scalar::fromF32(value));
}
__attribute__((noinline)) float visit_known_f32(float value) noexcept
{
    return *visitRead<float>(Storage(std::in_place_type<float>, value));
}
}
