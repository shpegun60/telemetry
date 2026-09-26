// Review repro (slots): shared named callables so two translation units compiled
// with different tiny_delegate configuration macros instantiate the same
// DelegateSlot/DelegateRefSlot specializations (lambdas would be TU-local).
#ifndef REVIEW_SLOTS_MACRO_ODR_SHARED_H
#define REVIEW_SLOTS_MACRO_ODR_SHARED_H
#include "Telemetry.h"

struct OdrReader {
    float value;
    float operator()() noexcept { return value; }
};
struct OdrWriter {
    float* target;
    telemetry::WriteResult operator()(float v) noexcept { *target = v; return telemetry::WriteResult::Applied; }
};

inline telemetry::DelegateSlot<float() noexcept> odrOwnedRead;
inline telemetry::DelegateSlot<telemetry::WriteResult(float) noexcept> odrOwnedWrite;
inline telemetry::DelegateRefSlot<float() noexcept> odrRefRead;
inline float odrStore = 0.f;
inline OdrReader odrBorrowed{7.f};
inline constexpr telemetry::FieldTable odrRows{
    telemetry::field("Owned", "", odrOwnedRead, odrOwnedWrite),
    telemetry::field("Ref", "", odrRefRead)};

// Every entry point is extern "C" so the two objects can be compared by name.
extern "C" void odr_bind_a() noexcept;
extern "C" void odr_bind_b() noexcept;
extern "C" float odr_read_a() noexcept;
extern "C" float odr_read_b() noexcept;
extern "C" telemetry::WriteResult odr_write_a(float) noexcept;
extern "C" telemetry::WriteResult odr_write_b(float) noexcept;
#endif
