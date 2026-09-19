// Compare inferred fields with their existing manual equivalents on Cortex-M7.
#include "field/TelemetryFieldFactory.h"
#include "catalog/TelemetryIndex.h"
#include "command/TelemetryCommandFactory.h"
#include "command/TelemetryCommandIndex.h"

using namespace telemetry;
struct FactoryOwner {
    float get() const noexcept;
    WriteResult set(float) noexcept;
    CommandResult calibrate(float, std::uint16_t) noexcept;
};
extern FactoryOwner factoryOwner;
float factoryFreeRead() noexcept;

extern "C" {
extern constexpr Field factory_probe_fields[] = {
#if defined(TELEMETRY_FACTORY_MANUAL)
    {0,"member","",ScalarType::F32,Getter::bind<&FactoryOwner::get>(factoryOwner)},
    {1,"free","",ScalarType::F32,Getter::bind<&factoryFreeRead>()},
#else
    makeField<&FactoryOwner::get>(0,"member","",factoryOwner),
    makeField<&factoryFreeRead>(1,"free",""),
#endif
};
float factory_known_read() noexcept {return factory_probe_fields[0].read<float>().value_or(0);}
float factory_free_read() noexcept {return factory_probe_fields[1].read<float>().value_or(0);}
Scalar factory_runtime_read(const Field& field) noexcept {return field.read();}
WriteResult factory_known_write(float value) noexcept
{
    constexpr auto field=makeField<&FactoryOwner::get,&FactoryOwner::set>(0,"rw","",factoryOwner);
    return field.write(value);
}
CommandResult factory_known_command(float v,std::uint16_t mode) noexcept
{
    constexpr auto command=makeCommand<&FactoryOwner::calibrate>(0,"calibrate",factoryOwner);
    return command.call(v,mode);
}
CommandResult factory_runtime_command(const Command& command,const Scalar* values,std::size_t count) noexcept
{
    return command.execute(values,count);
}
}
