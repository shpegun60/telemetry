#include "catalog/TelemetryCatalog.h"
#include <cstddef>
#include <cstdint>
#include <type_traits>

using telemetry::Field;
static_assert(std::is_standard_layout_v<Field>);
static_assert(std::is_standard_layout_v<telemetry::FieldType>);
static_assert(std::is_trivially_copyable_v<Field>);
static_assert(sizeof(void*) == 4, "Layout metrics require the ARM32 compiler");
#if TELEMETRY_LAYOUT_VARIANT == 2
static_assert(sizeof(Field) == 96 && alignof(Field) == 32);
static_assert(offsetof(Field, type) < 32 && offsetof(Field, flags) < 32);
static_assert(offsetof(Field, get) + sizeof(telemetry::Getter) <= 32);
static_assert(offsetof(Field, set) + sizeof(telemetry::Setter) <= 32);
#elif TELEMETRY_LAYOUT_VARIANT == 6
static_assert(sizeof(Field) == 96 && alignof(Field) == 32);
static_assert(offsetof(Field, get) == 0 && offsetof(Field, readType) == 8);
static_assert(offsetof(Field, set) == 32 && offsetof(Field, declaredType) == 40);
#elif TELEMETRY_LAYOUT_VARIANT == 5
static_assert(sizeof(Field) == 128 && alignof(Field) == 64);
static_assert(offsetof(Field, get) == 0 && offsetof(Field, declaredType) == 24);
#elif TELEMETRY_LAYOUT_VARIANT >= 3
static_assert(sizeof(Field) == 96 && alignof(Field) == 32);
#if TELEMETRY_LAYOUT_VARIANT == 4
static_assert(offsetof(Field, get) == 0 && offsetof(Field, declaredType) == 24);
#endif
#else
static_assert(sizeof(Field) == 80 && alignof(Field) == 8);
#endif

// Read these little-endian words from the ARM object, never from a host ABI.
extern "C" {
extern const std::uint32_t layout_metrics[] __attribute__((used, section(".rodata.layout_metrics"))) = {
    0x4c41594f, 1, sizeof(Field), alignof(Field), offsetof(Field, get), offsetof(Field, set),
#if TELEMETRY_LAYOUT_VARIANT == 2
    offsetof(Field, type), offsetof(Field, flags),
#elif TELEMETRY_LAYOUT_VARIANT == 6
    offsetof(Field, readType), UINT32_MAX,
#else
    UINT32_MAX, UINT32_MAX, // No duplicate members in A/B.
#endif
    offsetof(Field, declaredType),
#if TELEMETRY_LAYOUT_VARIANT == 6
    UINT32_MAX, // Position is the identity; no stored ID.
#else
    offsetof(Field, id),
#endif
    offsetof(Field, name), offsetof(Field, unit),
    sizeof(telemetry::Getter), sizeof(telemetry::Setter), sizeof(telemetry::FieldType), 1024,
};
}
