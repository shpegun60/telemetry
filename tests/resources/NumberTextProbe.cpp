// ARM integer width, numeric dependencies and stack probes (MIT).
// Authors: Ruslan Kovtun (shpegun60), codexAi.
#include <resource/telemetry/detail/Stream.hpp>

static_assert(sizeof(telemetry_resource::detail::Stream) == 48);

extern "C" bool resource_probe_u32(telemetry_resource::detail::Writer& writer,
                                   std::uint32_t value) noexcept
{
    return writer.integer(value);
}

extern "C" bool resource_probe_s32(telemetry_resource::detail::Writer& writer,
                                   std::int32_t value) noexcept
{
    return writer.integer(value);
}

extern "C" std::size_t resource_probe_float_text(double value, char (&output)[32]) noexcept
{
    return telemetry_resource::detail::floatingText(value, output);
}
