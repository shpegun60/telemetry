/*
 * @file CompositeStorage.cpp
 * @brief Storage-policy parity for small composites and DMI suppression.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#include <telemetry/model/Model.hpp>
namespace ts = telemetry;
namespace small {
inline int initializers = 0, writes = 0, services = 0;
int initialize() noexcept { ++initializers; return 4; }
struct Value { std::uint32_t a = initialize(); std::uint32_t b = initialize(); };
struct Nine { std::array<std::uint8_t, 9> bytes; };
Value get() noexcept { return {42, 17}; }
telemetry::WriteResult set(const Value& value) noexcept
{ ++writes; return value.a == 42 && value.b == 17 ? telemetry::WriteResult::Applied : telemetry::WriteResult::InvalidValue; }
telemetry::CommandResult call(const Value& value) noexcept
{ ++writes; return value.a == 42 && value.b == 17 ? telemetry::CommandResult::Executed : telemetry::CommandResult::InvalidValue; }
Value service(const Value& value) noexcept { ++services; return {value.a, value.b}; }
Nine nine() noexcept { return {}; }
inline constexpr ts::FieldTable fields{ts::field<&get, &set>("small"), ts::field<&nine>("nine")};
inline constexpr ts::CommandTable commands{ts::command<&call>("small")};
inline constexpr ts::FieldCatalogTable catalogs{ts::group("p", fields)};
inline constexpr ts::CommandCatalogTable actions{ts::group("p", commands)};
inline constexpr ts::ServiceTable replies{ts::service<&service>("Echo")};
inline constexpr ts::ServiceCatalogTable replyCatalogs{ts::group("p", replies)};
static_assert(fields.data()[0].readScratchBytes == (sizeof(Value) <= ts::maxLocalObjectBytes ? 0 : ts::scratchBytes<Value>));
static_assert(fields.data()[0].writeScratchBytes == fields.data()[0].readScratchBytes);
static_assert(fields.data()[1].readScratchBytes == (sizeof(Nine) <= ts::maxLocalObjectBytes ? 0 : ts::scratchBytes<Nine>));
static_assert(fields.data()[1].writeScratchBytes == 0);
static_assert(commands.data()[0].scratchBytes == fields.data()[0].readScratchBytes);
}
int main()
{
    ts::Workspace none{std::span<std::byte>{}};
    std::array<std::byte, 9> bytes{};
    if constexpr (sizeof(small::Value) > ts::maxLocalObjectBytes) {
        if (small::catalogs.index().readEncoded(0, bytes, none).dispatch != ts::DispatchStatus::WorkspaceTooSmall ||
            small::catalogs.index().writeEncoded(0, std::span{bytes}.first(8), none).dispatch != ts::DispatchStatus::WorkspaceTooSmall ||
            small::actions.index().executeEncoded(0, std::span{bytes}.first(8), none).dispatch != ts::DispatchStatus::WorkspaceTooSmall ||
            small::initializers != 0 || small::writes != 0) return 4;
    }
    std::array<std::byte, ts::scratchBytes<small::Value>> storage;
    ts::Workspace workspace{storage};
    const auto read = small::catalogs.index().readEncoded(0, bytes, workspace);
    if (read.dispatch != ts::DispatchStatus::Ok || read.written != 8 ||
        bytes[0] != std::byte{42} || bytes[4] != std::byte{17}) return 1;
    if (small::catalogs.index().writeEncoded(0, std::span{bytes}.first(8), workspace).endpointStatus != telemetry::WriteResult::Applied ||
        small::actions.index().executeEncoded(0, std::span{bytes}.first(8), workspace).endpointStatus != telemetry::CommandResult::Executed ||
        small::initializers != 0 || small::writes != 2) return 2;
    const auto expected = sizeof(small::Nine) <= ts::maxLocalObjectBytes
        ? ts::DispatchStatus::Ok : ts::DispatchStatus::WorkspaceTooSmall;
    if (small::catalogs.index().readEncoded(1, bytes, none).dispatch != expected) return 3;
    // The same DMI request takes the local or Workspace path under the budget
    // matrix. Neither path may execute its application initializers.
    bytes[0] = std::byte{42};
    bytes[4] = std::byte{17};
    std::array<std::byte, 64> serviceStorage{};
    ts::Workspace serviceWorkspace{serviceStorage};
    std::array<std::byte, 8> response{};
    const auto result = small::replyCatalogs.index().callEncoded(
        0u, std::span{bytes}.first(8), response, serviceWorkspace);
    if (result.dispatch != ts::DispatchStatus::Ok || result.written != 8 ||
        response[0] != std::byte{42} || response[4] != std::byte{17} ||
        small::services != 1 || small::initializers != 0 || serviceWorkspace.used() != 0) return 5;
    return 0;
}
