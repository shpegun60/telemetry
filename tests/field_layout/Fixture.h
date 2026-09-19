#ifndef TELEMETRY_LAYOUT_FIXTURE_H
#define TELEMETRY_LAYOUT_FIXTURE_H

#include "TelemetryEnum.h"
#include "TelemetryIndex.h"
#include <array>
#include <utility>

// Keep owners external: .data/.bss in the probe must remain empty.
extern volatile float layout_source_f32;
extern volatile double layout_source_f64;
extern volatile std::uint16_t layout_source_u16;
extern volatile float layout_sink_f32;
extern volatile std::uint16_t layout_sink_u16;

namespace layout_fixture {
using namespace telemetry;
enum class Mode : std::uint16_t { Off, Auto, Manual };

// Retain callback boundaries to distinguish a direct call from dispatch.
__attribute__((noinline)) inline float getF32() noexcept { return layout_source_f32; }
__attribute__((noinline)) inline double getF64() noexcept { return layout_source_f64; }
__attribute__((noinline)) inline std::uint16_t getU16() noexcept { return layout_source_u16; }
__attribute__((noinline)) inline WriteResult setF32(const Scalar& value) noexcept
{
    layout_sink_f32 = value.get<float>();
    return WriteResult::Applied;
}
__attribute__((noinline)) inline WriteResult setU16(const Scalar& value) noexcept
{
    layout_sink_u16 = value.get<std::uint16_t>();
    return WriteResult::Applied;
}

inline constexpr auto boundedFloat = numericType<float>(250, 1, 1000);
inline constexpr auto boundedInteger = numericType<std::uint16_t>(15, 10, 20);
inline constexpr auto enumInteger = enumType<Mode>();
inline constexpr auto plainEnumRange = numericType<std::uint16_t>(0, 0, 2);

constexpr Field row(std::size_t offset) noexcept
{
    const auto id = makeId(0, static_cast<FieldOffset>(offset));
    switch (offset % 8) {
        case 0: return {id, "native_float", "V", ScalarType::F32, getF32, setF32};
        case 1: return {id, "bounded_float", "V", boundedFloat, getF32, setF32};
        case 2: return {id, "native_integer", "", ScalarType::U16, getU16, setU16};
        case 3: return {id, "bounded_integer", "", boundedInteger, getU16, setU16};
        case 4: return {id, "enum_integer", "", enumInteger, getU16, setU16};
        case 5: return {id, "plain_enum_range", "", plainEnumRange, getU16, setU16};
        case 6: return {id, "normalized_integer", "", ScalarType::U16, getF64, setU16};
        default: return {id, "readonly", "V", ScalarType::F32, getF32};
    }
}

template <std::size_t... I>
constexpr auto rows(std::index_sequence<I...>) noexcept
{
    static_assert(sizeof...(I) <= idComponentCapacity);
    return std::array<Field, sizeof...(I)>{{row(I)...}};
}
} // namespace layout_fixture
#endif
