// Each selected misuse must fail compilation. Authors: Ruslan Kovtun, codexAi.
#include "Telemetry.h"
using namespace telemetry;
struct Owner {float read() const noexcept{return 1;} WriteResult write(float) noexcept{return WriteResult::Applied;}} owner;
CommandResult run(float) noexcept{return CommandResult::Executed;}
constexpr FieldTable rows{field<&Owner::read,&Owner::write>("x","",owner)};
constexpr FieldCatalogTable fields{group("f",rows)};
constexpr CommandTable commands{command<&run>("run")};
constexpr CommandCatalogTable actions{group("c",commands)};
#if TELEMETRY_TABLE_FAIL_CASE == 1
auto bad=rows.read<1>();
#elif TELEMETRY_TABLE_FAIL_CASE == 2
auto bad=rows.write<1>(2);
#elif TELEMETRY_TABLE_FAIL_CASE == 3
auto bad=fields.read<makeId(1,0)>();
#elif TELEMETRY_TABLE_FAIL_CASE == 4
auto bad=fields.write<makeId(1,0)>(2);
#elif TELEMETRY_TABLE_FAIL_CASE == 5
auto bad=fields.read<makeId(0,1),float>();
#elif TELEMETRY_TABLE_FAIL_CASE == 6
auto bad=rows.read<0,long double>();
#elif TELEMETRY_TABLE_FAIL_CASE == 7
auto bad=rows.write<0>("wrong");
#elif TELEMETRY_TABLE_FAIL_CASE == 8
auto bad=group("f",FieldTable{field<&Owner::read>("x","",owner)});
#elif TELEMETRY_TABLE_FAIL_CASE == 9
auto bad=FieldTable{field<&Owner::read>("x","",owner)}.data();
#elif TELEMETRY_TABLE_FAIL_CASE == 10
auto bad=FieldCatalogTable{group("f",rows)}.index();
#elif TELEMETRY_TABLE_FAIL_CASE == 11
auto bad=FieldCatalogTable{group("f",rows)}.data();
#elif TELEMETRY_TABLE_FAIL_CASE == 12
CatalogIndex bad=FieldCatalogTable{group("f",rows)};
#elif TELEMETRY_TABLE_FAIL_CASE == 13
auto bad=rows;
#elif TELEMETRY_TABLE_FAIL_CASE == 14
auto bad=field<&Owner::read>("x","",Owner{});
#elif TELEMETRY_TABLE_FAIL_CASE == 15
auto bad=FieldCatalogTable{group("commands",commands)};
#elif TELEMETRY_TABLE_FAIL_CASE == 16
auto bad=CommandCatalogTable{group("fields",rows)};
#elif TELEMETRY_TABLE_FAIL_CASE == 17
auto bad=actions.call<makeId(1,0)>(1.f);
#elif TELEMETRY_TABLE_FAIL_CASE == 18
auto bad=actions.call<0>(1);
#elif TELEMETRY_TABLE_FAIL_CASE == 19
auto bad=CommandCatalogTable{group("c",commands)}.index();
#elif TELEMETRY_TABLE_FAIL_CASE == 20
auto bad=group("c",CommandTable{command<&run>("run")});
#elif TELEMETRY_TABLE_FAIL_CASE == 21
auto bad=field("x","",[v=3]() noexcept {return v;});
#elif TELEMETRY_TABLE_FAIL_CASE == 22
auto bad=FieldTable{Field{}};
#elif TELEMETRY_TABLE_FAIL_CASE == 23
auto bad=field<&Owner::read>(0,"x","",owner);
#elif TELEMETRY_TABLE_FAIL_CASE == 24
auto bad=command<&run>(0,"run");
#elif TELEMETRY_TABLE_FAIL_CASE == 25
auto bad=telemetry::makeField<&Owner::read>("x","",owner);
#elif TELEMETRY_TABLE_FAIL_CASE == 26
auto bad=telemetry::makeCommand<&run>("run");
#else
#error Select table failure case
#endif
int main(){}
