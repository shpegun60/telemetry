// Compare direct, local typed, global typed, static-ID and dynamic access on ARM.
// Authors: Ruslan Kovtun (shpegun60), codexAi. License: MIT.
#include "Telemetry.h"
using namespace telemetry;
enum class TableMode : std::uint16_t { Off, Auto, Manual };
struct TableOwner {
    float read() const noexcept;
    WriteResult write(float) noexcept;
    std::uint16_t count() const noexcept;
    WriteResult count(std::uint16_t) noexcept;
    TableMode mode() const noexcept;
    WriteResult mode(TableMode) noexcept;
};
extern TableOwner tableOwner;
constexpr auto countGet=static_cast<std::uint16_t(TableOwner::*)() const noexcept>(&TableOwner::count);
constexpr auto countSet=static_cast<WriteResult(TableOwner::*)(std::uint16_t) noexcept>(&TableOwner::count);
constexpr auto modeGet=static_cast<TableMode(TableOwner::*)() const noexcept>(&TableOwner::mode);
constexpr auto modeSet=static_cast<WriteResult(TableOwner::*)(TableMode) noexcept>(&TableOwner::mode);
extern constexpr FieldTable field_table_probe{
    field<&TableOwner::read,&TableOwner::write>("voltage","V",tableOwner,limits(2.f,0.f,500.f)),
    field<countGet,countSet>("count","",tableOwner),
    field<modeGet,modeSet>("mode","",tableOwner)};
constexpr FieldCatalogTable field_global_probe{group("values",field_table_probe)};
constexpr Catalog field_compat_groups[]={{"values",field_table_probe.data(),field_table_probe.size()}};
constexpr auto field_compat_index=CatalogIndex::bind<field_compat_groups>();
static_assert(sizeof(field_table_probe)==sizeof(Field)*3);
static_assert(sizeof(Field)==96 && sizeof(Catalog)==12 && sizeof(Command)==20);

enum class ProbeField : std::uint64_t { Voltage, Count, Mode };
#ifdef TELEMETRY_ENUM_POSITION_PROBE
constexpr auto voltagePosition = ProbeField::Voltage;
constexpr auto countPosition = ProbeField::Count;
constexpr auto modePosition = ProbeField::Mode;
#else
constexpr std::size_t voltagePosition = 0, countPosition = 1, modePosition = 2;
#endif

extern "C" {
float table_direct_read() noexcept {return tableOwner.read();}
float table_local_read() noexcept {return field_table_probe.read<voltagePosition>().value_or(-1);}
float table_global_read() noexcept {return field_global_probe.read<0>().value_or(-1);}
float table_compat_read() noexcept {return field_compat_index.read<0>().value_or(-1);}
std::uint16_t table_direct_converted() noexcept {return detail::readNumber<std::uint16_t>(tableOwner.read()).value_or(0);}
std::uint16_t table_local_converted() noexcept {return field_table_probe.read<voltagePosition,std::uint16_t>().value_or(0);}
std::uint16_t table_global_converted() noexcept {return field_global_probe.read<0,std::uint16_t>().value_or(0);}
WriteResult table_direct_write(float x) noexcept {
    if(!(x>=0.f && x<=500.f))return WriteResult::InvalidValue;
    return tableOwner.write(x);
}
WriteResult table_local_write(float x) noexcept {return field_table_probe.write<voltagePosition>(x);}
WriteResult table_global_write(float x) noexcept {return field_global_probe.write<0>(x);}
WriteResult table_compat_write(float x) noexcept {return field_compat_index.write<0>(x);}
WriteResult table_direct_int(int x) noexcept {
    const float value=static_cast<float>(x);
    if(!(value>=0.f && value<=500.f))return WriteResult::InvalidValue;
    return tableOwner.write(value);
}
WriteResult table_local_int(int x) noexcept {return field_table_probe.write<voltagePosition>(x);}
WriteResult table_global_int(int x) noexcept {return field_global_probe.write<0>(x);}
WriteResult table_direct_u16(std::uint16_t x) noexcept {return tableOwner.count(x);}
WriteResult table_local_u16(std::uint16_t x) noexcept {return field_table_probe.write<countPosition>(x);}
WriteResult table_global_u16(std::uint16_t x) noexcept {return field_global_probe.write<1>(x);}
WriteResult table_direct_enum(std::uint16_t x) noexcept {
    if(x>2)return WriteResult::InvalidValue;
    return tableOwner.mode(static_cast<TableMode>(x));
}
WriteResult table_local_enum(std::uint16_t x) noexcept {return field_table_probe.write<modePosition>(x);}
WriteResult table_global_enum(std::uint16_t x) noexcept {return field_global_probe.write<2>(x);}
Scalar table_runtime_read(FieldId id) noexcept {return field_global_probe.read(id);}
WriteResult table_runtime_write(FieldId id,float x) noexcept {return field_global_probe.write(id,x);}
float table_runtime_owner(const decltype(field_table_probe)& table) noexcept {return table.read<voltagePosition>().value_or(-1);}
}
