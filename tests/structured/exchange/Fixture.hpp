/* Stage 11 native model and canonical packet fixtures. MIT. */
#pragma once
#include <resource/structured/Bind.hpp>
#include <resource/structured/Exchange.hpp>
#include <resource/structured/DescriptorFile.hpp>
#include <resource/structured/ValuesFile.hpp>
#include <resource/FileSystem.hpp>
#include <algorithm>
#include <array>
#include <cassert>

namespace fixture {
namespace ts = telemetry::structured;
namespace rs = resource::structured;
using D = ts::DispatchStatus;
using W = telemetry::WriteResult;
using C = telemetry::CommandResult;
using S = ts::ServiceStatus;
using Op = rs::Operation;
using resource::Input;
using resource::Output;

struct Config { std::uint32_t value; bool enabled; };
struct Big { std::array<std::uint32_t, 1024> words; };
struct Device {
    Config current{42, true};
    unsigned reads = 0, writes = 0, commands = 0, services = 0;
    W writeStatus = W::Applied;
    C commandStatus = C::Executed;
    S serviceStatus = S::Ok;

    Config read() noexcept { ++reads; return current; }
    W write(const Config& value) noexcept { ++writes; current = value; return writeStatus; }
    C configure(const Config& value) noexcept { ++commands; current = value; return commandStatus; }
    C reset() noexcept { ++commands; return commandStatus; }
    ts::ServiceResult<Config> echo(const Config& value) noexcept
    {
        ++services;
        if (serviceStatus != S::Ok) return ts::ServiceResult<Config>::failure(serviceStatus);
        return ts::ServiceResult<Config>::success(value);
    }
    ts::ServiceResult<void> ping() noexcept
    {
        ++services;
        return serviceStatus == S::Ok ? ts::ServiceResult<void>::success()
                                     : ts::ServiceResult<void>::failure(serviceStatus);
    }
    ts::ServiceResult<Big> large(const Config& value) noexcept
    {
        ++services;
        current = value; // A deliberate side effect must not precede capacity checks.
        if (serviceStatus != S::Ok) return ts::ServiceResult<Big>::failure(serviceStatus);
        return ts::ServiceResult<Big>::successFrom([&]() noexcept { return Big{{value.value}}; });
    }
    Big readBig() noexcept { ++reads; return Big{{current.value}}; }
    W writeBig(const Big& value) noexcept { ++writes; current.value = value.words.back(); return writeStatus; }
    C commandBig(const Big& value) noexcept { ++commands; current.value = value.words.back(); return commandStatus; }
};
inline Device device;
inline telemetry::OwnerSlot<Device> slot;
inline constexpr ts::FieldTable localFields{
    ts::field<&Device::read, &Device::write>("Config", device),
    ts::field<&Device::read>("ReadOnly", device),
    ts::field<&Device::read, &Device::write>("Optional", slot),
    ts::field<&Device::readBig, &Device::writeBig>("Big", device)};
inline constexpr ts::CommandTable localCommands{
    ts::command<&Device::configure>("Configure", device),
    ts::command<&Device::reset>("Reset", device),
    ts::command<&Device::configure>("Optional", slot),
    ts::command<&Device::commandBig>("Big", device)};
inline constexpr ts::ServiceTable localServices{
    ts::service<&Device::echo>("Echo", device),
    ts::service<&Device::ping>("Ping", device),
    ts::service<&Device::echo>("Optional", slot),
    ts::service<&Device::large>("Large", device)};
inline constexpr ts::FieldCatalogTable fields{ts::group("device", localFields)};
inline constexpr ts::CommandCatalogTable commands{ts::group("device", localCommands)};
inline constexpr ts::ServiceCatalogTable services{ts::group("device", localServices)};
inline constexpr ts::Model model{fields, commands, services};
inline constexpr auto view = model.view();
inline constexpr rs::Descriptor descriptor{model};
inline constexpr auto packed = rs::packDescriptor<descriptor>();
inline constexpr rs::DescriptorFile descriptorFile{packed};

// Test helpers deliberately do not use the protocol implementation's helpers.
template <class U>
void put(Output bytes, std::size_t offset, U value) noexcept
{
    for (std::size_t i = 0; i < sizeof(U); ++i) bytes[offset + i] = std::byte(value >> (i * 8));
}
inline std::uint32_t get32(Input bytes, std::size_t offset) noexcept
{
    std::uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i)
        value |= std::uint32_t(std::to_integer<unsigned>(bytes[offset + i])) << (8 * i);
    return value;
}
inline std::array<std::byte, 16> handshake(std::uint64_t fingerprint = descriptor.fingerprint()) noexcept
{
    std::array<std::byte, 16> result{std::byte{'T'}, std::byte{'S'}, std::byte{'B'}, std::byte{'N'}, std::byte{3}};
    put(result, 8, fingerprint);
    return result;
}
inline void header(Output bytes, Op operation, std::uint32_t endpoint,
                   std::uint32_t payload, std::uint32_t correlation = 0x10203040) noexcept
{
    assert(bytes.size() >= 24);
    std::fill_n(bytes.begin(), 24, std::byte{0});
    bytes[0] = std::byte{'T'}; bytes[1] = std::byte{'S'};
    bytes[2] = std::byte{'R'}; bytes[3] = std::byte{'Q'}; bytes[4] = std::byte{3};
    put(bytes, 8, correlation); put(bytes, 12, endpoint); put(bytes, 16, payload);
    bytes[20] = static_cast<std::byte>(operation);
}
template <class T>
std::array<std::byte, 24 + ts::wireSize<T>> packet(Op operation, std::uint32_t id, const T& value) noexcept
{
    std::array<std::byte, 24 + ts::wireSize<T>> result{};
    header(result, operation, id, ts::wireSize<T>);
    const auto status = ts::encode(value, Output{result}.subspan(24));
    assert(status == ts::CodecStatus::Ok);
    (void)status;
    return result;
}
inline std::array<std::byte, 24> emptyPacket(Op operation, std::uint32_t id) noexcept
{
    std::array<std::byte, 24> result{};
    header(result, operation, id, 0);
    return result;
}
inline void ready(rs::Binding& peer) noexcept
{
    std::array<std::byte, 8> response{};
    const auto result = rs::Bind::process(peer, view, descriptor.fingerprint(), handshake(), response);
    assert(result.dispatch == D::Ok && result.written == 8 && peer.ready());
}
inline unsigned callbackCount() noexcept { return device.writes + device.commands + device.services; }
} // namespace fixture
