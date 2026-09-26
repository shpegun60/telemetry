// Critic trial: the same device as a Cortex-M7 image, to measure what an adopter pays in Flash/RAM.
// Variants (preprocessor): none = empty baseline; CRITIC_CORE = runtime read/write/execute by ID;
// CRITIC_JSON = plus schema/values/command JSON; CRITIC_RES = plus binary resources and the
// packet protocol (implemented in CriticFirmwareResources.cpp, C++20).
#if defined(CRITIC_CORE)
#include "CriticDevice.h"
#endif
#if defined(CRITIC_JSON)
#include "serialization/TelemetryCommandJson.h"
#include "serialization/TelemetryJson.h"
#endif
#include <cstddef>
#include <cstdint>

// A stand-in for a transport: the host side writes requests here, the firmware answers here.
volatile std::uint32_t transportId;
volatile float transportValue;
volatile std::uint32_t transportResult;

#if defined(CRITIC_JSON)
char schemaJson[1024];
char valuesJson[128];
char commandJson[512];
#endif

#if defined(CRITIC_RES)
extern "C" std::size_t criticHandle(const std::uint8_t* request, std::size_t size,
                                     std::uint8_t* response, std::size_t capacity) noexcept;
std::uint8_t packetIn[32];
std::uint8_t packetOut[256];
#endif

int main()
{
    for (;;) {
        const std::uint32_t id = transportId;
#if defined(CRITIC_CORE)
        const auto value = critic::fieldIndex.read<float>(id);
        const auto written = critic::fieldIndex.write(id, transportValue);
#if defined(CRITIC_NO_EXEC)
        const int executed = 0; // Fields only: no command descriptor is referenced.
#elif defined(CRITIC_BARE_COMMANDS)
        // Same two commands without arg<> metadata, to isolate the parameter-description cost.
        static constexpr telemetry::CommandTable bare{
            telemetry::command<&critic::Device::reset>("Reset", critic::device),
            telemetry::command<&critic::Device::configure>("Configure", critic::device)};
        static constexpr telemetry::CommandCatalogTable bareCatalog{telemetry::group("meter", bare)};
        const telemetry::Scalar args[] = {transportValue, std::uint8_t{1}};
        const auto executed = bareCatalog.index().execute(id, args, 2);
#else
        const telemetry::Scalar args[] = {transportValue, std::uint8_t{1}};
        const auto executed = critic::commandIndex.execute(id, args, 2);
#endif
        transportResult = (value ? static_cast<std::uint32_t>(*value) : 0u) + static_cast<std::uint32_t>(written) +
                          static_cast<std::uint32_t>(executed);
#endif
#if defined(CRITIC_JSON)
        transportResult = transportResult + telemetry::writeSchema(critic::fieldIndex, schemaJson, sizeof(schemaJson)) +
                          telemetry::writeValues(critic::fieldIndex, valuesJson, sizeof(valuesJson)) +
                          telemetry::writeSchema(critic::commandIndex, commandJson, sizeof(commandJson));
#endif
#if defined(CRITIC_RES)
        transportResult = transportResult + criticHandle(packetIn, sizeof(packetIn), packetOut, sizeof(packetOut));
#endif
        if (id == 0xffffffffu) {
            break;
        }
    }
    return 0;
}
