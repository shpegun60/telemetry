#include "command/TelemetryCommandCatalogTable.h"
#include "Telemetry.h"
#include "tiny_delegate.hpp"
using namespace telemetry;
enum class Mode : unsigned char { Off, On };
enum class Other : unsigned char { Off, On };
struct Device {
    float get() const noexcept { return 0; }
    Scalar scalar() const noexcept { return 0.0f; }
    float throwing() const { return 0; }
    Mode mode() const noexcept { return Mode::Off; }
    WriteResult mismatch(unsigned short) noexcept { return WriteResult::Applied; }
    WriteResult other(Other) noexcept { return WriteResult::Applied; }
    void badReturn(float) noexcept {}
    WriteResult set(float) noexcept { return WriteResult::Applied; }
    CommandResult action(float) noexcept { return CommandResult::Executed; }
    CommandResult throwingCommand(float) { return CommandResult::Executed; }
    CommandResult reference(float&) noexcept { return CommandResult::Executed; }
    CommandResult pointer(const char*) noexcept { return CommandResult::Executed; }
    CommandResult scalarCommand(Scalar) noexcept { return CommandResult::Executed; }
    CommandResult pair(float, Mode) noexcept { return CommandResult::Executed; }
    CommandResult enumAction(Mode) noexcept { return CommandResult::Executed; }
};
Device device;
const Device constant;
CommandResult global(float) noexcept {return CommandResult::Executed;}
float parameterRead() noexcept {return 0;}
WriteResult parameterWrite(float) noexcept {return WriteResult::Applied;}
WriteResult parameterMismatch(unsigned short) noexcept {return WriteResult::Applied;}
WriteResult parameterThrowing(float) {return WriteResult::Applied;}
CommandResult commandThrowing(float) {return CommandResult::Executed;}
Mode parameterMode() noexcept {return Mode::Off;}
WriteResult parameterModeWrite(Mode) noexcept {return WriteResult::Applied;}
struct ThrowingCallable { CommandResult operator()(float) { return CommandResult::Executed; } };
struct OverloadedCallable {
    CommandResult operator()(float) noexcept { return CommandResult::Executed; }
    CommandResult operator()(int) noexcept { return CommandResult::Executed; }
};
ThrowingCallable throwingCallable;
OverloadedCallable overloadedCallable;
auto genericCallable=[](auto) noexcept {return CommandResult::Executed;};
auto stableCallable=[](float) noexcept {return CommandResult::Executed;};
struct OverloadedFieldGetter {
    float operator()() noexcept { return 0; }
    float operator()(int) noexcept { return 0; }
};
OverloadedFieldGetter overloadedFieldGetter;
constexpr auto one = commandArgs(arg("v","",1.0f));
constexpr auto two = commandArgs(arg("v"),arg("x"));
constexpr auto wrong = commandArgs(arg("v","",1));

#if TELEMETRY_FACTORY_FAIL_CASE == 1
constexpr auto bad=telemetry::field<&Device::get,&Device::mismatch>("x","",device);
#elif TELEMETRY_FACTORY_FAIL_CASE == 2
constexpr auto bad=telemetry::field<&Device::mode,&Device::other>("x","",device);
#elif TELEMETRY_FACTORY_FAIL_CASE == 3
constexpr auto bad=telemetry::field<&Device::get,&Device::badReturn>("x","",device);
#elif TELEMETRY_FACTORY_FAIL_CASE == 4
constexpr auto bad=telemetry::field<&Device::throwing>("x","",device);
#elif TELEMETRY_FACTORY_FAIL_CASE == 5
auto bad=telemetry::field<&Device::get>("x","",Device{});
#elif TELEMETRY_FACTORY_FAIL_CASE == 6
constexpr auto bad=telemetry::field<&Device::scalar>("x","",device);
#elif TELEMETRY_FACTORY_FAIL_CASE == 7
constexpr auto bad=telemetry::field<&Device::get>("x","",device,limits(1,0,5));
#elif TELEMETRY_FACTORY_FAIL_CASE == 8
constexpr auto bad=telemetry::field<&Device::get>("x","",device,limits(6.0f,0.0f,5.0f));
#elif TELEMETRY_FACTORY_FAIL_CASE == 9
constexpr auto bad=telemetry::field<&Device::get,&Device::set>("x","",constant);
#elif TELEMETRY_FACTORY_FAIL_CASE == 10
auto bad=detail::materializeCommand<&Device::action>("x",Device{});
#elif TELEMETRY_FACTORY_FAIL_CASE == 11
auto bad=detail::materializeCommand<&Device::action>("x",device,commandArgs(arg("v")));
#elif TELEMETRY_FACTORY_FAIL_CASE == 12
auto bad=detail::materializeCommand<&global>("x",commandArgs(arg("v")));
#elif TELEMETRY_FACTORY_FAIL_CASE == 13
constexpr auto bad=detail::materializeCommand<&Device::action>("x",device,two);
#elif TELEMETRY_FACTORY_FAIL_CASE == 14
constexpr auto bad=detail::materializeCommand<&Device::action>("x",device,wrong);
#elif TELEMETRY_FACTORY_FAIL_CASE == 15
constexpr auto invalid=commandArgs(arg("v","",6.0f,0.0f,5.0f));
constexpr auto bad=detail::materializeCommand<&Device::action>("x",device,invalid);
#elif TELEMETRY_FACTORY_FAIL_CASE == 16
constexpr auto bad=detail::materializeCommand<&Device::throwingCommand>("x",device);
#elif TELEMETRY_FACTORY_FAIL_CASE == 17
constexpr auto bad=detail::materializeCommand<&Device::reference>("x",device);
#elif TELEMETRY_FACTORY_FAIL_CASE == 18
constexpr auto bad=detail::materializeCommand<&Device::pointer>("x",device);
#elif TELEMETRY_FACTORY_FAIL_CASE == 19
constexpr auto bad=detail::materializeCommand<&Device::scalarCommand>("x",device);
#elif TELEMETRY_FACTORY_FAIL_CASE == 20
constexpr auto bad=detail::materializeCommand<&Device::get>("x",device);
#elif TELEMETRY_FACTORY_FAIL_CASE == 21
constexpr auto bad=detail::materializeCommand<&Device::action>("x",constant);
#elif TELEMETRY_FACTORY_FAIL_CASE == 22
constexpr auto target=static_cast<CommandResult (*)(float) noexcept>(nullptr);
constexpr auto bad=detail::materializeCommand<target>("x");
#elif TELEMETRY_FACTORY_FAIL_CASE == 23
using Commands=Command[1];
auto bad=CommandIndex{Commands{}};
#elif TELEMETRY_FACTORY_FAIL_CASE == 24
constexpr auto bad=detail::materializeCommand<&Device::action>("x",device);
auto result=bad.call("12");
#elif TELEMETRY_FACTORY_FAIL_CASE == 25
constexpr auto invalid=commandArgs(arg(nullptr));
constexpr auto bad=detail::materializeCommand<&global>("x",invalid);
#elif TELEMETRY_FACTORY_FAIL_CASE == 26
constexpr auto adapter=+[](const Device& owner) noexcept {return owner.get();};
auto bad=Getter::bindContext<adapter>(Device{});
#elif TELEMETRY_FACTORY_FAIL_CASE == 27
constexpr auto adapter=+[](const Device& owner) noexcept {return owner.get();};
auto bad=tiny::delegate_ref<float()>::bind_context<adapter>(Device{});
#elif TELEMETRY_FACTORY_FAIL_CASE == 28
constexpr auto bad=telemetry::field<&Device::get>("x","",ScalarType::F64,device);
#elif TELEMETRY_FACTORY_FAIL_CASE == 29
auto build(){float value=1;return telemetry::field("x","",[value]() noexcept {return value;});}
#elif TELEMETRY_FACTORY_FAIL_CASE == 30
constexpr auto target=static_cast<float (*)() noexcept>(nullptr);
constexpr auto bad=telemetry::field("x","",target);
#elif TELEMETRY_FACTORY_FAIL_CASE == 31
auto bad=telemetry::field("x","",[](){return 1.0f;});
#elif TELEMETRY_FACTORY_FAIL_CASE == 32
auto bad=telemetry::field("x","",parameterRead,parameterMismatch);
#elif TELEMETRY_FACTORY_FAIL_CASE == 33
auto bad=telemetry::field("x","",parameterRead,parameterThrowing);
#elif TELEMETRY_FACTORY_FAIL_CASE == 34
auto build(){float state=0;return telemetry::field("x","",parameterRead,
    [state](float) noexcept {return WriteResult::Applied;});}
#elif TELEMETRY_FACTORY_FAIL_CASE == 35
auto bad=detail::materializeCommand("x",commandThrowing);
#elif TELEMETRY_FACTORY_FAIL_CASE == 36
auto build(){float state=0;return detail::materializeCommand("x",
    [state](float) noexcept {return CommandResult::Executed;});}
#elif TELEMETRY_FACTORY_FAIL_CASE == 37
auto bad=detail::materializeCommand("x",global,commandArgs(arg("v")));
#elif TELEMETRY_FACTORY_FAIL_CASE == 38
auto bad=telemetry::field<&parameterRead>("x","",parameterWrite);
#elif TELEMETRY_FACTORY_FAIL_CASE == 39
constexpr auto bad=telemetry::field("x","",parameterMode,parameterModeWrite);
#elif TELEMETRY_FACTORY_FAIL_CASE == 40
constexpr auto bad=detail::materializeCommand("x",global);
#elif TELEMETRY_FACTORY_FAIL_CASE == 41
auto bad=detail::materializeCommand("x",throwingCallable);
#elif TELEMETRY_FACTORY_FAIL_CASE == 42
auto bad=detail::materializeCommand("x",genericCallable);
#elif TELEMETRY_FACTORY_FAIL_CASE == 43
auto bad=detail::materializeCommand("x",overloadedCallable);
#elif TELEMETRY_FACTORY_FAIL_CASE == 44
auto bad=detail::materializeCommand("x",stableCallable,commandArgs(arg("v")));
#elif TELEMETRY_FACTORY_FAIL_CASE == 45
constexpr auto bad=telemetry::field<&Device::mode>("x","",device,enumSpec<Other::Off,Other::On>());
#elif TELEMETRY_FACTORY_FAIL_CASE == 46
constexpr auto bad=enumSpec<Mode::Off,Other::On>();
#elif TELEMETRY_FACTORY_FAIL_CASE == 47
constexpr auto bad=enumSpec<Mode::Off,Mode::Off>();
#elif TELEMETRY_FACTORY_FAIL_CASE == 48
constexpr auto metadata=commandArgs(arg("mode","",enumSpec<Other::Off,Other::On>()));
constexpr auto bad=detail::materializeCommand<&Device::action>("x",device,metadata);
#elif TELEMETRY_FACTORY_FAIL_CASE == 49
constexpr auto bad=telemetry::field("x","",parameterMode);
#elif TELEMETRY_FACTORY_FAIL_CASE == 50
auto bad=detail::materializeCommand("x",[](float) noexcept {return CommandResult::Executed;});
#elif TELEMETRY_FACTORY_FAIL_CASE == 51
using CommandRows=Command[1];
auto bad=CommandCatalog{"temporary",CommandRows{},1};
#elif TELEMETRY_FACTORY_FAIL_CASE == 52
using CommandGroups=CommandCatalog[1];
auto bad=CommandCatalogIndex{CommandGroups{},1};
#elif TELEMETRY_FACTORY_FAIL_CASE == 53
constexpr auto duplicate=commandArgs(arg<0>("a"),arg<0>("b"));
constexpr auto bad=detail::materializeCommand<&Device::action>("x",device,duplicate);
#elif TELEMETRY_FACTORY_FAIL_CASE == 54
constexpr auto outside=commandArgs(arg<1>("outside"));
constexpr auto bad=detail::materializeCommand<&Device::action>("x",device,outside);
#elif TELEMETRY_FACTORY_FAIL_CASE == 55
constexpr auto wrongIndexed=commandArgs(arg<0>("v","",1));
constexpr auto bad=detail::materializeCommand<&Device::action>("x",device,wrongIndexed);
#elif TELEMETRY_FACTORY_FAIL_CASE == 56
constexpr auto mixed=commandArgs(arg("positional"),arg<0>("indexed"));
constexpr auto bad=detail::materializeCommand<&Device::action>("x",device,mixed);
#elif TELEMETRY_FACTORY_FAIL_CASE == 57
auto build(){float state=0;auto get=[&state]() noexcept{return state;};
    return telemetry::field("x","",get,[&state](float value) noexcept {
        state=value;return WriteResult::Applied;});}
#elif TELEMETRY_FACTORY_FAIL_CASE == 58
auto build(){float state=0;auto get=[&state]() noexcept{return state;};
    auto set=[&state](unsigned short value) noexcept {state=value;return WriteResult::Applied;};
    return telemetry::field("x","",get,set);}
#elif TELEMETRY_FACTORY_FAIL_CASE == 59
constexpr auto wrongEnum=commandArgs(arg<0>("mode","",Other::On));
constexpr auto bad=detail::materializeCommand<&Device::enumAction>("x",device,wrongEnum);
#elif TELEMETRY_FACTORY_FAIL_CASE == 60
constexpr auto duplicateOrder=commandArgs(arg<1>("m"),arg<0>("v"),arg<1>("again"));
constexpr auto bad=detail::materializeCommand<&Device::pair>("x",device,duplicateOrder);
#elif TELEMETRY_FACTORY_FAIL_CASE == 61
constexpr auto bad=CommandTable{command<&global>("x",arg("value","",1.0f))};
#elif TELEMETRY_FACTORY_FAIL_CASE == 62
auto bad=CommandTable{command("x",[](float) noexcept {return CommandResult::Executed;},
    arg<0>("value","",1.0f))};
#elif TELEMETRY_FACTORY_FAIL_CASE == 63
constexpr auto sourceTable=CommandTable{command<&global>("x",arg<0>("value","",1.0f))};
auto bad=sourceTable;
#elif TELEMETRY_FACTORY_FAIL_CASE == 64
constexpr CommandTable sourceRows{command<&global>("x",arg<0>("value","",1.0f))};
constexpr CommandCatalogTable sourceCatalog{group("group",sourceRows)};
auto bad=sourceCatalog;
#elif TELEMETRY_FACTORY_FAIL_CASE == 65
auto build(){float state=0;auto get=[&state](){return state;};return telemetry::field("x","",get);}
#elif TELEMETRY_FACTORY_FAIL_CASE == 66
auto build(){float state=0;auto get=[&state]() noexcept{return state;};
    auto set=[&state](float value){state=value;return WriteResult::Applied;};
    return telemetry::field("x","",get,set);}
#elif TELEMETRY_FACTORY_FAIL_CASE == 67
auto build(){float state=0;auto get=[&state](auto...) noexcept{return state;};
    return telemetry::field("x","",get);}
#elif TELEMETRY_FACTORY_FAIL_CASE == 68
auto bad=telemetry::field("x","",overloadedFieldGetter);
#elif TELEMETRY_FACTORY_FAIL_CASE == 69
auto build(){float state=0;const auto get=[state]() mutable noexcept{return state;};
    return telemetry::field("x","",get);}
#elif TELEMETRY_FACTORY_FAIL_CASE == 70
auto build(){float state=0;auto get=[&state]() noexcept{return state;};
    return telemetry::field("x","",ScalarType::F32,get);}
#elif TELEMETRY_FACTORY_FAIL_CASE == 71
auto bad=CommandTable{command<&global>("x",arg<0>("value","",1.0f))}.index();
#elif TELEMETRY_FACTORY_FAIL_CASE == 72
auto bad=CommandTable{command<&global>("x",arg<0>("value","",1.0f))}.data();
#elif TELEMETRY_FACTORY_FAIL_CASE == 73
auto bad=CommandTable{command<&global>("x",arg<0>("value","",1.0f))}[0];
#elif TELEMETRY_FACTORY_FAIL_CASE == 74
constexpr CommandTable sourceRows{command<&global>("x",arg<0>("value","",1.0f))};
auto bad=CommandCatalogTable{group("group",sourceRows)}.index();
#elif TELEMETRY_FACTORY_FAIL_CASE == 75
constexpr CommandTable sourceRows{command<&global>("x",arg<0>("value","",1.0f))};
constexpr CommandCatalogTable sourceCatalog{group("group",sourceRows)};
auto bad=sourceCatalog.call<makeId(1,0)>(1.0f);
#elif TELEMETRY_FACTORY_FAIL_CASE == 76
constexpr CommandTable sourceRows{command<&global>("x",arg<0>("value","",1.0f))};
auto bad=CommandCatalogTable{group("group",sourceRows)}.data();
#elif TELEMETRY_FACTORY_FAIL_CASE == 77
constexpr auto sourceTable=CommandTable{command<&global>("x",arg<0>("value","",1.0f))};
auto bad=sourceTable.call<1>(1.0f);
#elif TELEMETRY_FACTORY_FAIL_CASE == 78
constexpr auto sourceTable=CommandTable{command<&global>("x",arg<0>("value","",1.0f))};
auto bad=sourceTable.call<0>();
#elif TELEMETRY_FACTORY_FAIL_CASE == 79
constexpr auto sourceTable=CommandTable{command<&global>("x",arg<0>("value","",1.0f))};
auto bad=sourceTable.call<0>("x");
#elif TELEMETRY_FACTORY_FAIL_CASE == 80
constexpr auto sourceTable=CommandTable{command<&global>("x",arg<0>("value","",1.0f))};
auto bad=sourceTable.call(std::size_t{0},"x");
#elif TELEMETRY_FACTORY_FAIL_CASE == 81
constexpr auto sourceTable=CommandTable{command<&global>("x",arg<0>("value","",1.0f))};
auto bad=sourceTable.call<0>(1);
#elif TELEMETRY_FACTORY_FAIL_CASE == 82
constexpr auto sourceTable=CommandTable{command<&Device::pair>("x",device)};
auto bad=sourceTable.call<0>(1.0f,std::uint8_t{0});
#else
#error Unknown factory case
#endif
int main() {}
