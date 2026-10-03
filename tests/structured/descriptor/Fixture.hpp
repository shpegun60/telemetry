/*
 * @file Fixture.hpp
 * @brief Frozen v3 models: mixed endpoints, shared types, UTF-8 and empty shapes.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "../endpoints/MixedFixture.hpp"
#include <resource/telemetry/v3/Descriptor.hpp>

namespace descriptor_fixture {
namespace ts = telemetry;
namespace rs = resource::telemetry::v3;
struct Empty {};
struct Box {
    fixture::MotorConfig config;
    std::array<fixture::State, 2> phases;
    Empty empty;
    std::array<std::uint8_t, 0> zero;
};
enum class Signed : std::int16_t { Low = -1000, High = 2000 };
enum class Wide : std::uint64_t { High = 0xffffffffffffffffULL };
}

template <> struct telemetry::reflection::EnumReflection<descriptor_fixture::Signed> {
    inline static constexpr auto entries = telemetry::reflection::enumEntries(
        telemetry::reflection::enumEntry(descriptor_fixture::Signed::High, u8"Haut"),
        telemetry::reflection::enumEntry(descriptor_fixture::Signed::Low, u8"N\u00e9gatif"));
};
template <> struct telemetry::reflection::EnumReflection<descriptor_fixture::Wide> {
    inline static constexpr auto entries = telemetry::reflection::enumEntries(
        telemetry::reflection::enumEntry(descriptor_fixture::Wide::High, u8"Max"));
};

namespace descriptor_fixture {
inline int calls = 0;
template <class T> T read() noexcept { ++calls; return {}; }
inline void ping() noexcept { ++calls; }
inline telemetry::FunctionSlot<std::uint32_t() noexcept> getter;
inline telemetry::FunctionSlot<telemetry::WriteResult(std::uint32_t) noexcept> setter;
inline constexpr ts::FieldTable extraFields{
    ts::field<&read<Empty>>("Empty"),
    ts::field<&read<Box>>("Box"),
    ts::field<&read<Signed>>("Signed"),
    ts::field<&read<Wide>>("Wide"),
    ts::field("Late", getter, setter)};
inline constexpr ts::FieldTable<> noFields{};
inline constexpr ts::ServiceTable extraServices{ts::service<&ping>("Ping")};
inline constexpr ts::FieldCatalogTable fields{
    ts::group("motor", fixture::mixedFields),
    ts::group("empty", noFields),
    ts::group("\xCE\xBC", extraFields)};
inline constexpr ts::ServiceCatalogTable services{
    ts::group("motor", fixture::localServices),
    ts::group("extra", extraServices)};
inline constexpr ts::Model model{fields, fixture::commands, services};
inline constexpr rs::Descriptor mixed{fixture::model};
inline constexpr rs::Descriptor edge{model};
inline constexpr ts::ServiceCatalogTable<> noServices{};
inline constexpr ts::Model emptyModel{ts::emptyFields, ts::emptyCommands, noServices};
inline constexpr rs::Descriptor empty{emptyModel};
inline constexpr auto mixedBytes = rs::packDescriptor<mixed>();
inline constexpr auto edgeBytes = rs::packDescriptor<edge>();
inline constexpr auto emptyBytes = rs::packDescriptor<empty>();
static_assert(mixed.valid() && edge.valid() && empty.valid());
static_assert(decltype(model)::typeId<fixture::MotorConfig>() == 15);
}
