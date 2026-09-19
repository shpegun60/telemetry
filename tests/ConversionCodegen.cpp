// Compile-only conversion probes. Use the Cortex-M7 flags from IndexCodegen.cpp
// at both -O2 and -Os, then inspect the object with arm-none-eabi-objdump -dr -C.
#include "TelemetryConversion.h"

extern "C" {
__attribute__((noinline)) bool convert_f32_f32(float value, float& output) noexcept
{
    return telemetry::detail::convertNumberTo(value, output);
}
__attribute__((noinline)) bool convert_f32_f64(float value, double& output) noexcept
{
    return telemetry::detail::convertNumberTo(value, output);
}
__attribute__((noinline)) bool convert_f64_f32(double value, float& output) noexcept
{
    return telemetry::detail::convertNumberTo(value, output);
}
__attribute__((noinline)) bool convert_u16_u32(std::uint16_t value, std::uint32_t& output) noexcept
{
    return telemetry::detail::convertNumberTo(value, output);
}
__attribute__((noinline)) bool convert_f32_bool(float value, bool& output) noexcept
{
    return telemetry::detail::convertNumberTo(value, output);
}
__attribute__((noinline)) bool convert_f32_u32(float value, std::uint32_t& output) noexcept
{
    return telemetry::detail::convertNumberTo(value, output);
}
__attribute__((noinline)) bool convert_f64_s64(double value, std::int64_t& output) noexcept
{
    return telemetry::detail::convertNumberTo(value, output);
}
__attribute__((noinline)) bool convert_f64_u64(double value, std::uint64_t& output) noexcept
{
    return telemetry::detail::convertNumberTo(value, output);
}
__attribute__((noinline)) bool convert_s64_u64(std::int64_t value, std::uint64_t& output) noexcept
{
    return telemetry::detail::convertNumberTo(value, output);
}
__attribute__((noinline)) bool convert_scalar_f32(float value, telemetry::Scalar& output) noexcept
{
    return telemetry::convertScalar(telemetry::Scalar::fromF32(value), telemetry::ScalarType::F32, output);
}
__attribute__((noinline)) bool convert_scalar_f64(float value, telemetry::Scalar& output) noexcept
{
    return telemetry::convertScalar(telemetry::Scalar::fromF32(value), telemetry::ScalarType::F64, output);
}
__attribute__((noinline)) bool convert_scalar_identity(const telemetry::Scalar& value, telemetry::Scalar& output) noexcept
{
    return telemetry::convertScalar(value, value.type(), output);
}
}
