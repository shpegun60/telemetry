// Storage comparison against the previous tag/union layout. Compile with the
// Cortex-M7 flags from IndexCodegen.cpp at -O2 and -Os; compare each pair with
// arm-none-eabi-objdump -dr -C and arm-none-eabi-nm -S --size-sort.
#include "TelemetryConversion.h"

namespace {
struct LegacyScalar {
    telemetry::ScalarType type;
    union {
        float floatValue;
        double doubleValue;
        std::uint32_t unsignedValue;
        std::int32_t signedValue;
        std::uint64_t unsigned64Value;
        bool boolValue;
        std::uint8_t unsigned8Value;
        std::uint16_t unsigned16Value;
        std::int8_t signed8Value;
        std::int16_t signed16Value;
        std::int64_t signed64Value;
    };
    constexpr explicit LegacyScalar(float value) noexcept
        : type(telemetry::ScalarType::F32), floatValue(value) {}
};

constexpr bool copyAlternatives()
{
    telemetry::Scalar value;
    value = telemetry::Scalar::fromF32(12.5f);
    if (value.type() != telemetry::ScalarType::F32 || value.get<float>() != 12.5f) return false;
    value = telemetry::Scalar::fromU64(UINT64_MAX);
    return value.get<std::uint64_t>() == UINT64_MAX && value.getIf<float>() == nullptr;
}
static_assert(copyAlternatives(), "Scalar copy assignment remains constexpr in C++17");
static_assert(sizeof(telemetry::Scalar) == sizeof(LegacyScalar));
static_assert(alignof(telemetry::Scalar) == alignof(LegacyScalar));
static_assert(std::is_trivially_copyable_v<telemetry::Scalar>);
}

extern "C" {
__attribute__((noinline)) LegacyScalar legacy_create(float value) noexcept { return LegacyScalar(value); }
__attribute__((noinline)) telemetry::Scalar variant_create(float value) noexcept { return telemetry::Scalar::fromF32(value); }
__attribute__((noinline)) float legacy_read(const LegacyScalar& value) noexcept
{
    return value.type == telemetry::ScalarType::F32 ? value.floatValue : -1.0f;
}
__attribute__((noinline)) float variant_read(const telemetry::Scalar& value) noexcept
{
    return value.type() == telemetry::ScalarType::F32 ? value.get<float>() : -1.0f;
}
__attribute__((noinline)) bool legacy_convert(const LegacyScalar& value, std::uint32_t& output) noexcept
{
    return value.type == telemetry::ScalarType::F32 && telemetry::detail::convertNumberTo(value.floatValue, output);
}
__attribute__((noinline)) bool variant_convert(const telemetry::Scalar& value, std::uint32_t& output) noexcept
{
    return value.type() == telemetry::ScalarType::F32 && telemetry::detail::convertNumberTo(value.get<float>(), output);
}
}
