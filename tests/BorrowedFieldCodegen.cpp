// Borrowed field factories must retain the compact immutable ARM layout.
#include "Telemetry.h"

namespace {
struct Reader {
    float value;
    float operator()() const noexcept { return value; }
};
struct Writer {
    float* value;
    telemetry::WriteResult operator()(float next) noexcept
    {
        *value = next;
        return telemetry::WriteResult::Applied;
    }
};
extern Reader reader;
extern Writer writer;
} // namespace

extern "C" const telemetry::Field telemetry_probe_borrowed_field =
    telemetry::field("Borrowed", "V", reader, writer,
                         telemetry::limits(1.0f, 0.0f, 10.0f)).materialize();

static_assert(sizeof(telemetry::Getter) == sizeof(void*) * 2);
static_assert(sizeof(telemetry::Setter) == sizeof(void*) * 2);
static_assert(sizeof(telemetry::Field) == 96 && alignof(telemetry::Field) == 32);

extern "C" float borrowed_field_read() noexcept
{
    const auto value = telemetry_probe_borrowed_field.read<float>();
    return value ? *value : -1.0f;
}

extern "C" telemetry::WriteResult borrowed_field_write(float value) noexcept
{
    return telemetry_probe_borrowed_field.write(value);
}
