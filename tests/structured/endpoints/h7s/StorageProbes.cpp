/*
 * @file StorageProbes.cpp
 * @brief Equal-work payloads for compile-time local/Workspace comparison.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#include "Fixture.hpp"
#include <cstring>

namespace bench {

__attribute__((noinline))
std::uint32_t resolvedRead(const void* raw, std::uint32_t, ts::Workspace& workspace) noexcept
{
    const auto& entry = *static_cast<const ts::FieldEntry*>(raw);
    std::array<std::byte, 4> output{};
#ifdef ENDPOINT_POINTER_THUNKS
    const auto result = entry.readEncoded(output, workspace);
#else
    const auto result = entry.read(entry.definition, output, workspace);
#endif
    asm volatile("" : "+m"(output) : : "memory");
    std::uint32_t value;
    std::memcpy(&value, output.data(), 4);
    return result.dispatch == ts::DispatchStatus::Ok ? value + result.written : 0;
}

__attribute__((noinline))
std::uint32_t resolvedWrite(const void* raw, std::uint32_t, ts::Workspace& workspace) noexcept
{
    const auto& entry = *static_cast<const ts::FieldEntry*>(raw);
#ifdef ENDPOINT_POINTER_THUNKS
    const auto result = entry.writeEncoded(input, workspace);
#else
    const auto result = entry.write(entry.definition, input, workspace);
#endif
    return result.dispatch == ts::DispatchStatus::Ok && result.endpointStatus == telemetry::WriteResult::Applied;
}

__attribute__((noinline))
std::uint32_t resolvedCommand(const void* raw, std::uint32_t, ts::Workspace& workspace) noexcept
{
    const auto& entry = *static_cast<const ts::CommandEntry*>(raw);
#ifdef ENDPOINT_POINTER_THUNKS
    const auto result = entry.executeEncoded(input, workspace);
#else
    const auto result = entry.invoke(entry.definition, input, workspace);
#endif
    return result.dispatch == ts::DispatchStatus::Ok && result.endpointStatus == telemetry::CommandResult::Executed;
}

template <std::size_t Bytes>
struct Block { std::array<std::uint32_t, Bytes / 4> words; };

template <std::size_t Bytes>
struct BlockDevice {
    Block<Bytes> value = [] {
        Block<Bytes> result{};
        result.words.front() = 17;
        result.words.back() = 3;
        return result;
    }();
    Block<Bytes> read() const noexcept { return value; }
    telemetry::WriteResult write(const Block<Bytes>& next) noexcept
    { value = next; return telemetry::WriteResult::Applied; }
    telemetry::CommandResult command(const Block<Bytes>& next) noexcept
    { value = next; return telemetry::CommandResult::Executed; }
    Block<Bytes> service(const Block<Bytes>& request) const noexcept
    {
        Block<Bytes> result{};
        for (std::size_t i = 0; i < result.words.size(); ++i)
            result.words[i] = value.words[i] + request.words[i];
        return result;
    }
};

template <std::size_t N> inline BlockDevice<N> blockDevice;
template <std::size_t N> inline constexpr ts::FieldTable blockFields{
    ts::field<&BlockDevice<N>::read, &BlockDevice<N>::write>("block", blockDevice<N>)};
template <std::size_t N> inline constexpr ts::CommandTable blockCommands{
    ts::command<&BlockDevice<N>::command>("set", blockDevice<N>)};
template <std::size_t N> inline constexpr ts::ServiceTable blockServices{
    ts::service<&BlockDevice<N>::service>("sum", blockDevice<N>)};
template <std::size_t N> inline constexpr ts::FieldCatalog blockFieldCatalog{"blocks", blockFields<N>.data(), 1};
template <std::size_t N> inline constexpr ts::CommandCatalog blockCommandCatalog{"blocks", blockCommands<N>.data(), 1};
template <std::size_t N> inline constexpr ts::ServiceCatalog blockServiceCatalog{"blocks", blockServices<N>.data(), 1};
template <std::size_t N> inline constexpr ts::FieldIndex blockFieldIndex{&blockFieldCatalog<N>, 1};
template <std::size_t N> inline constexpr ts::CommandIndex blockCommandIndex{&blockCommandCatalog<N>, 1};
template <std::size_t N> inline constexpr ts::ServiceIndex blockServiceIndex{&blockServiceCatalog<N>, 1};

// Mutable input keeps decode at runtime. All words are copied/encoded, and a
// compiler memory barrier retains the complete output, including middle words.
template <std::size_t N> inline std::array<std::byte, N> blockInput = [] {
    std::array<std::byte, N> result{};
    result[0] = std::byte{17};
    result[N - 4] = std::byte{3};
    return result;
}();

template <std::size_t N>
std::uint32_t consume(std::array<std::byte, N>& output, std::uint32_t written) noexcept
{
    asm volatile("" : "+m"(output) : : "memory");
    std::uint32_t first, last;
    std::memcpy(&first, output.data(), 4);
    std::memcpy(&last, output.data() + N - 4, 4);
    return first + last + written;
}

template <std::size_t N> __attribute__((noinline))
std::uint32_t blockRead(const void* raw, std::uint32_t id, ts::Workspace& workspace) noexcept
{
    std::array<std::byte, N> output{};
    const auto result = static_cast<const ts::FieldIndex*>(raw)->readEncoded(id, output, workspace);
    return result.dispatch == ts::DispatchStatus::Ok ? consume(output, result.written) : 0;
}
template <std::size_t N> __attribute__((noinline))
std::uint32_t blockWrite(const void* raw, std::uint32_t id, ts::Workspace& workspace) noexcept
{
    const auto result = static_cast<const ts::FieldIndex*>(raw)->writeEncoded(id, blockInput<N>, workspace);
    return result.dispatch == ts::DispatchStatus::Ok && result.endpointStatus == telemetry::WriteResult::Applied;
}
template <std::size_t N> __attribute__((noinline))
std::uint32_t blockCommand(const void* raw, std::uint32_t id, ts::Workspace& workspace) noexcept
{
    const auto result = static_cast<const ts::CommandIndex*>(raw)->executeEncoded(id, blockInput<N>, workspace);
    return result.dispatch == ts::DispatchStatus::Ok && result.endpointStatus == telemetry::CommandResult::Executed;
}
template <std::size_t N> __attribute__((noinline))
std::uint32_t blockService(const void* raw, std::uint32_t id, ts::Workspace& workspace) noexcept
{
    std::array<std::byte, N> output{};
    const auto result = static_cast<const ts::ServiceIndex*>(raw)->callEncoded(id, blockInput<N>, output, workspace);
    return result.dispatch == ts::DispatchStatus::Ok && result.endpointStatus == ts::ServiceStatus::Ok
        ? consume(output, result.written) : 0;
}

const void* const sizeIndexes[12]{
    &blockFieldIndex<16>, &blockFieldIndex<16>, &blockCommandIndex<16>, &blockServiceIndex<16>,
    &blockFieldIndex<32>, &blockFieldIndex<32>, &blockCommandIndex<32>, &blockServiceIndex<32>,
    &blockFieldIndex<64>, &blockFieldIndex<64>, &blockCommandIndex<64>, &blockServiceIndex<64>};
const Probe sizeProbes[12]{
    &blockRead<16>, &blockWrite<16>, &blockCommand<16>, &blockService<16>,
    &blockRead<32>, &blockWrite<32>, &blockCommand<32>, &blockService<32>,
    &blockRead<64>, &blockWrite<64>, &blockCommand<64>, &blockService<64>};

} // namespace bench
