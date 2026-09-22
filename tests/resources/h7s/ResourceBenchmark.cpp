// DWT and complete nested-stack measurements, compatible with f1cfbd8 and ABI 8.
// Authors: Ruslan Kovtun (shpegun60), codexAi. License: MIT.
#include <telemetry/Telemetry.h>
#include <resource/telemetry/TelemetryFiles.hpp>
#include "usart.h"
#include "uart_bench.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <new>

BenchCounter g_bench_usart_irq{}, g_bench_rx_dma_irq{}, g_bench_tx_dma_irq{};
extern "C" std::size_t telemetry_call_on_psp(std::size_t (*)() noexcept, void*) noexcept;

namespace
{
using namespace telemetry;
using namespace telemetry_resource;
constexpr unsigned groups = 256, iterations = 256, repetitions = 5, operations = 8;
enum class Mode : std::uint16_t
{
    Off,
    Auto,
    Manual
};
unsigned getters = 0;

float voltage() noexcept
{
    ++getters;
    return 230.f;
}

Mode mode() noexcept
{
    ++getters;
    return Mode::Auto;
}

CommandResult configure(float, Mode) noexcept
{
    return CommandResult::Executed;
}

constexpr FieldTable fieldRows{field<&voltage>("Limit", "V", limits(230.f, 0.f, 300.f)),
                               field<&mode>("Mode", "")};
constexpr CommandTable commandRows{command<&configure>(
    "Configure", arg<0>("Limit", "V", 230.f, 0.f, 300.f), arg<1>("Mode", "", Mode::Auto))};

template <std::size_t... I>
constexpr auto fieldGroups(std::index_sequence<I...>) noexcept
{
    return std::array<Catalog, sizeof...(I)>{
        ((void)I, Catalog{"g", fieldRows.data(), fieldRows.size()})...};
}

template <std::size_t... I>
constexpr auto commandGroups(std::index_sequence<I...>) noexcept
{
    return std::array<CommandCatalog, sizeof...(I)>{
        ((void)I, CommandCatalog{"g", commandRows.data(), commandRows.size()})...};
}

constexpr auto catalogs = fieldGroups(std::make_index_sequence<groups>{});
constexpr auto commands = commandGroups(std::make_index_sequence<groups>{});
alignas(SchemaFile) std::byte schemaStorage[sizeof(SchemaFile)];
alignas(CommandsFile) std::byte commandsStorage[sizeof(CommandsFile)];
alignas(ValuesFile) std::byte valuesStorage[sizeof(ValuesFile)];
SchemaFile* schema;
CommandsFile* commandFile;
ValuesFile* values;
std::byte buffer[256];
constexpr unsigned guard = 256, stackSize = 16384;
alignas(32) __attribute__((
    section(".resource_probe_stack"))) volatile unsigned char probeStack[guard + stackSize];
unsigned active;
bool valid = true;

constexpr resource::Cursor cursor(unsigned file, unsigned group) noexcept
{
#if TELEMETRY_LAYOUT_VARIANT == 7
    // Frozen f1cfbd8 ordinal format: six schema/seven command records per
    // group, two values per group. Select the enum field or whole command.
    const auto ordinal = file == 0 ? 4 + 6 * group : file == 1 ? 2 + 7 * group : 2 + 2 * group;
    return resource::Cursor{ordinal} << 32;
#else
    return (UINT64_C(2) << 62) |
           (resource::Cursor{makeId(static_cast<GroupId>(group), file == 1 ? 0 : 1)} << 30);
#endif
}

std::uint32_t checksum(resource::Input bytes, std::uint32_t sum = 2166136261u) noexcept
{
    for (auto byte : bytes)
    {
        sum = (sum ^ std::to_integer<unsigned>(byte)) * 16777619u;
    }
    return sum;
}

__attribute__((noinline, noipa)) std::uint32_t enumVisit(const FieldType& type) noexcept
{
    std::uint32_t sum = 0;
    const bool ok =
        type.describeEnum(&sum,
                          [](void* raw, const Scalar& value, std::string_view name) noexcept
                          {
                              *static_cast<std::uint32_t*>(raw) +=
                                  value.get<std::uint16_t>() + static_cast<unsigned>(name.size());
                              return true;
                          });
    valid = valid && ok;
    return sum;
}

__attribute__((noinline, noipa)) std::uint32_t parameterVisit(const Command& command) noexcept
{
    std::uint32_t sum = 0;
    const bool ok = command.forEachParameter(
        [&](const CommandParam& p) noexcept
        {
            sum += static_cast<unsigned>(p.index) + (p.name != nullptr ? std::strlen(p.name) : 0u);
            return true;
        });
    valid = valid && ok;
    return sum;
}

__attribute__((noinline, noipa)) std::uint32_t exercise(unsigned operation) noexcept
{
    if (operation == 6)
    {
        return enumVisit(fieldRows[1].declaredType);
    }
    if (operation == 7)
    {
        return parameterVisit(commandRows[0]);
    }
    const unsigned file = operation / 2, group = operation % 2 ? groups - 1 : 0;
    const auto position = cursor(file, group);
    const auto before = getters;
    const auto out = resource::Output{buffer, file == 2 ? 3u : sizeof buffer};
    const auto result = file == 0   ? schema->read(position, out)
                        : file == 1 ? commandFile->read(position, out)
                                    : values->read(position, out);
    valid = valid && result.status == resource::Status::Ok && result.written != 0 &&
            result.written <= out.size() && getters == before + (file == 2 ? 1u : 0u);
    return checksum(out.first(result.written));
}

__attribute__((noinline)) std::size_t stackExercise() noexcept
{
    return exercise(active);
}

__attribute__((noinline)) std::size_t stackControl() noexcept
{
    volatile unsigned char data[512];
    for (unsigned i = 0; i < sizeof data; ++i)
    {
        data[i] = static_cast<unsigned char>(i);
    }
    return data[511] == 255 ? 0 : 1;
}

__attribute__((noinline, noipa)) std::uint32_t loop(unsigned operation, unsigned count) noexcept
{
    std::uint32_t sum = 0;
    for (unsigned i = 0; i < count; ++i)
    {
        sum += exercise(operation);
    }
    return sum;
}

void line(const char* text)
{
    if (HAL_UART_Transmit(&huart3, reinterpret_cast<const std::uint8_t*>(text),
                          static_cast<std::uint16_t>(std::strlen(text)), 5000) != HAL_OK)
    {
        Error_Handler();
    }
}

// These checks run before timing and use the same bounded read contract as a
// transport. Two chunk sizes must produce the same complete bytes/checksum.
template <class File>
void checkFile(const File& file, unsigned number, unsigned chunk)
{
    resource::Cursor position = 0;
    std::uint32_t sum = 2166136261u, bytes = 0;
    const auto before = getters;
    bool ended = false;
    for (unsigned calls = 0; calls < file.size() + 1; ++calls)
    {
        const auto result = file.read(position, resource::Output{buffer, chunk});
        if (result.status != resource::Status::Ok || result.written > chunk ||
            (result.written == 0 && !result.eof) || (!result.eof && result.next == position))
        {
            valid = false;
            break;
        }
        sum = checksum(resource::Input{buffer, result.written}, sum);
        bytes += result.written;
        position = result.next;
        if (result.eof)
        {
            const auto again = file.read(position, resource::Output{buffer, chunk});
            ended = again.status == resource::Status::Ok && again.eof && again.written == 0;
            break;
        }
    }
    valid = valid && ended && bytes == file.size() &&
            getters == before + (number == 2 ? groups * 2 : 0);
    char report[112];
    std::snprintf(report, sizeof report, "RESOURCE F %u %u %lu %lu %u\r\n", number, chunk,
                  static_cast<unsigned long>(bytes), static_cast<unsigned long>(sum),
                  getters - before);
    line(report);
}

void checkContracts() noexcept
{
    const auto before = getters;
    const auto position = cursor(2, groups - 1);
    const auto small = values->read(position, resource::Output{buffer, 2});
    const auto bad = values->read(position + 1, resource::Output{buffer, 3});
    valid = valid && small.status == resource::Status::BufferTooSmall && small.written == 0 &&
            small.next == position && bad.status == resource::Status::InvalidCursor &&
            bad.written == 0 && getters == before;

#if TELEMETRY_LAYOUT_VARIANT != 7
    const auto& type = fieldRows[1].declaredType;
    unsigned count = 0, sum = 0;
    const auto sink = +[](void* raw, const Scalar& value, std::string_view name) noexcept
    {
        if (value.type() != ScalarType::U16)
        {
            return false;
        }
        *static_cast<unsigned*>(raw) += value.get<std::uint16_t>() + name.size();
        return true;
    };
    valid = valid && type.enumCount() == 3 && commandRows[0].parameterCount() == 2;
    for (unsigned i = 0; i < 3; ++i)
    {
        const bool visited = type.describeEnumEntry(i, &sum, sink);
        valid = valid && visited;
    }
    for (unsigned i = 0; i < 2; ++i)
    {
        const bool visited =
            commandRows[0].visitParameter(i,
                                          [&](const CommandParam& param) noexcept
                                          {
                                              ++count;
                                              return param.index == i && param.name != nullptr;
                                          });
        valid = valid && visited;
    }
    const bool extra = commandRows[0].visitParameter(2,
                                                     [&](const CommandParam&) noexcept
                                                     {
                                                         ++count;
                                                         return true;
                                                     });
    valid = valid && sum == 16 && count == 2 && !extra && !type.describeEnumEntry(3, &sum, sink) &&
            sum == 16;
#endif
}
} // namespace

extern "C" void bench_init()
{
    MPU_Region_InitTypeDef region{};
    HAL_MPU_Disable();
    region.Enable = MPU_REGION_ENABLE;
    region.Number = MPU_REGION_NUMBER1;
    region.BaseAddress = 0x24000000;
    region.Size = MPU_REGION_SIZE_512KB;
    region.AccessPermission = MPU_REGION_FULL_ACCESS;
    region.TypeExtField = MPU_TEX_LEVEL1;
    region.IsCacheable = MPU_ACCESS_CACHEABLE;
    region.IsBufferable = MPU_ACCESS_BUFFERABLE;
    region.IsShareable = MPU_ACCESS_NOT_SHAREABLE;
    region.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
    HAL_MPU_ConfigRegion(&region);

    // ES0596, section 2.2.17: even speculative access to this unused H7RS
    // GFXMMU window can stall the bus. The Cube scaffold's background map
    // otherwise leaves it Normal/cacheable. Device + XN forbids speculation.
    region.Number = MPU_REGION_NUMBER2;
    region.BaseAddress = 0x25000000;
    region.Size = MPU_REGION_SIZE_16MB;
    region.TypeExtField = MPU_TEX_LEVEL0;
    region.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
    region.IsBufferable = MPU_ACCESS_BUFFERABLE;
    region.IsShareable = MPU_ACCESS_SHAREABLE;
    region.AccessPermission = MPU_REGION_NO_ACCESS;
    HAL_MPU_ConfigRegion(&region);
    HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
    SCB_EnableICache();
    SCB_EnableDCache();
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    __DSB();
    __ISB();
}

extern "C" void bench_loop()
{
    std::uint8_t input;
    if (HAL_UART_Receive(&huart3, &input, 1, 100) != HAL_OK)
    {
        return;
    }
    // Reset always returns to idle, including after an interrupted test.
    // The host checks this handshake before requesting any resource work.
    if (input == 'P')
    {
        line("RESOURCE IDLE 2\r\n");
        return;
    }
    if (input != 'R')
    {
        return;
    }
    valid = true;
    getters = 0;
    if (schema == nullptr)
    {
        schema = new (schemaStorage) SchemaFile{CatalogIndex{catalogs.data(), catalogs.size()}};
        commandFile = new (commandsStorage)
            CommandsFile{CommandCatalogIndex{commands.data(), commands.size()}};
        values = new (valuesStorage) ValuesFile{*schema};
    }
    if (SystemCoreClock != 600000000u || __get_IPSR() != 0 || (__get_CONTROL() & 3u) != 0 ||
        (SCB->CCR & (SCB_CCR_IC_Msk | SCB_CCR_DC_Msk)) != (SCB_CCR_IC_Msk | SCB_CCR_DC_Msk) ||
        schema->size() == 0 || commandFile->size() == 0)
    {
        line("RESOURCE FAIL setup\r\n");
        return;
    }
    char report[192];
    std::snprintf(report, sizeof report, "RESOURCE READY 2 %u %u %lu %u %u %u %u %lu %lu %lu\r\n",
                  TELEMETRY_LAYOUT_VARIANT, LAYOUT_OPT, static_cast<unsigned long>(SystemCoreClock),
                  groups, iterations, repetitions, operations,
                  static_cast<unsigned long>(schema->size()),
                  static_cast<unsigned long>(commandFile->size()),
                  static_cast<unsigned long>(values->size()));
    line(report);
    valid = valid && getters == 0;
    checkContracts();
    for (unsigned chunk : {31u, 256u})
    {
        checkFile(*schema, 0, chunk);
        checkFile(*commandFile, 1, chunk);
        checkFile(*values, 2, chunk);
    }
    if (!valid)
    {
        line("RESOURCE FAIL contracts\r\n");
        return;
    }
    for (unsigned operation = 0; operation < operations; ++operation)
    {
        for (unsigned repeat = 0; repeat < repetitions; ++repeat)
        {
            const auto mask = __get_PRIMASK();
            __disable_irq();
            SCB_CleanInvalidateDCache();
            SCB_InvalidateICache();
            volatile auto warm = loop(operation, 32);
            (void)warm;
            __DSB();
            __ISB();
            const auto begin = DWT->CYCCNT;
            const auto sum = loop(operation, iterations);
            __DSB();
            const auto cycles = DWT->CYCCNT - begin;
            __set_PRIMASK(mask);
            std::snprintf(report, sizeof report, "RESOURCE T %u %u %lu %lu\r\n", operation, repeat,
                          static_cast<unsigned long>(cycles), static_cast<unsigned long>(sum));
            line(report);
        }
    }
    for (active = 0; active <= operations; ++active)
    {
        for (unsigned pattern = 0; pattern < 2; ++pattern)
        {
            const unsigned char fill = pattern == 0 ? 0xa5 : 0x5a;
            for (auto& byte : probeStack)
            {
                byte = fill;
            }
            const auto mask = __get_PRIMASK(), control = __get_CONTROL(), psp = __get_PSP();
            __disable_irq();
            const auto result =
                telemetry_call_on_psp(active == operations ? stackControl : stackExercise,
                                      const_cast<unsigned char*>(probeStack) + sizeof probeStack);
            const bool restored = control == __get_CONTROL() && psp == __get_PSP();
            __set_PRIMASK(mask);
            unsigned first = 0;
            while (first < sizeof probeStack && probeStack[first] == fill)
            {
                ++first;
            }
            const auto used = sizeof probeStack - first;
            valid = valid && restored && first >= guard &&
                    (active != operations || (used >= 512 && result == 0));
            std::snprintf(report, sizeof report, "RESOURCE S %u %u %lu %lu\r\n", active, pattern,
                          static_cast<unsigned long>(used), static_cast<unsigned long>(result));
            line(report);
        }
    }
    line(valid ? "RESOURCE DONE\r\n" : "RESOURCE FAIL validation\r\n");
}
