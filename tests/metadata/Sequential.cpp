// The same source is compiled against the baseline and current headers.
#include "Telemetry.h"
enum class Mode : std::uint16_t { Off, Auto, Manual };
extern telemetry::CommandResult configure(float, Mode) noexcept;
constexpr auto type = telemetry::enumType<Mode>();
constexpr telemetry::CommandTable table{telemetry::command<&configure>("Configure",
    telemetry::arg<0>("Limit", "V", 230.f, 0.f, 500.f))};
extern "C" bool metadata_known_enum(void* ctx, telemetry::EnumEntrySink sink) noexcept
{ return type.describeEnum(ctx, sink); }
extern "C" bool metadata_runtime_enum(const telemetry::FieldType& t, void* ctx, telemetry::EnumEntrySink sink) noexcept
{ return t.describeEnum(ctx, sink); }
extern "C" bool metadata_known_command(void* ctx, telemetry::CommandParamSink sink) noexcept
{ return table[0].describeParameters(ctx, sink); }
extern "C" bool metadata_runtime_command(const telemetry::Command& c, void* ctx, telemetry::CommandParamSink sink) noexcept
{ return c.describeParameters(ctx, sink); }
