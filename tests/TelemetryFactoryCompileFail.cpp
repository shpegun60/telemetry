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
constexpr auto bad=makeField<&Device::get,&Device::mismatch>(0,"x","",device);
#elif TELEMETRY_FACTORY_FAIL_CASE == 2
constexpr auto bad=makeField<&Device::mode,&Device::other>(0,"x","",device);
#elif TELEMETRY_FACTORY_FAIL_CASE == 3
constexpr auto bad=makeField<&Device::get,&Device::badReturn>(0,"x","",device);
#elif TELEMETRY_FACTORY_FAIL_CASE == 4
constexpr auto bad=makeField<&Device::throwing>(0,"x","",device);
#elif TELEMETRY_FACTORY_FAIL_CASE == 5
auto bad=makeField<&Device::get>(0,"x","",Device{});
#elif TELEMETRY_FACTORY_FAIL_CASE == 6
constexpr auto bad=makeField<&Device::scalar>(0,"x","",device);
#elif TELEMETRY_FACTORY_FAIL_CASE == 7
constexpr auto bad=makeField<&Device::get>(0,"x","",device,limits(1,0,5));
#elif TELEMETRY_FACTORY_FAIL_CASE == 8
constexpr auto bad=makeField<&Device::get>(0,"x","",device,limits(6.0f,0.0f,5.0f));
#elif TELEMETRY_FACTORY_FAIL_CASE == 9
constexpr auto bad=makeField<&Device::get,&Device::set>(0,"x","",constant);
#elif TELEMETRY_FACTORY_FAIL_CASE == 10
auto bad=makeCommand<&Device::action>(0,"x",Device{});
#elif TELEMETRY_FACTORY_FAIL_CASE == 11
auto bad=makeCommand<&Device::action>(0,"x",device,commandArgs(arg("v")));
#elif TELEMETRY_FACTORY_FAIL_CASE == 12
auto bad=makeCommand<&global>(0,"x",commandArgs(arg("v")));
#elif TELEMETRY_FACTORY_FAIL_CASE == 13
constexpr auto bad=makeCommand<&Device::action>(0,"x",device,two);
#elif TELEMETRY_FACTORY_FAIL_CASE == 14
constexpr auto bad=makeCommand<&Device::action>(0,"x",device,wrong);
#elif TELEMETRY_FACTORY_FAIL_CASE == 15
constexpr auto invalid=commandArgs(arg("v","",6.0f,0.0f,5.0f));
constexpr auto bad=makeCommand<&Device::action>(0,"x",device,invalid);
#elif TELEMETRY_FACTORY_FAIL_CASE == 16
constexpr auto bad=makeCommand<&Device::throwingCommand>(0,"x",device);
#elif TELEMETRY_FACTORY_FAIL_CASE == 17
constexpr auto bad=makeCommand<&Device::reference>(0,"x",device);
#elif TELEMETRY_FACTORY_FAIL_CASE == 18
constexpr auto bad=makeCommand<&Device::pointer>(0,"x",device);
#elif TELEMETRY_FACTORY_FAIL_CASE == 19
constexpr auto bad=makeCommand<&Device::scalarCommand>(0,"x",device);
#elif TELEMETRY_FACTORY_FAIL_CASE == 20
constexpr auto bad=makeCommand<&Device::get>(0,"x",device);
#elif TELEMETRY_FACTORY_FAIL_CASE == 21
constexpr auto bad=makeCommand<&Device::action>(0,"x",constant);
#elif TELEMETRY_FACTORY_FAIL_CASE == 22
constexpr auto target=static_cast<CommandResult (*)(float) noexcept>(nullptr);
constexpr auto bad=makeCommand<target>(0,"x");
#elif TELEMETRY_FACTORY_FAIL_CASE == 23
using Commands=Command[1];
auto bad=CommandIndex{Commands{}};
#elif TELEMETRY_FACTORY_FAIL_CASE == 24
constexpr auto bad=makeCommand<&Device::action>(0,"x",device);
auto result=bad.call("12");
#elif TELEMETRY_FACTORY_FAIL_CASE == 25
constexpr auto invalid=commandArgs(arg(nullptr));
constexpr auto bad=makeCommand<&global>(0,"x",invalid);
#elif TELEMETRY_FACTORY_FAIL_CASE == 26
constexpr auto adapter=+[](const Device& owner) noexcept {return owner.get();};
auto bad=Getter::bindContext<adapter>(Device{});
#elif TELEMETRY_FACTORY_FAIL_CASE == 27
constexpr auto adapter=+[](const Device& owner) noexcept {return owner.get();};
auto bad=tiny::delegate_ref<float()>::bind_context<adapter>(Device{});
#elif TELEMETRY_FACTORY_FAIL_CASE == 28
constexpr auto bad=makeField<&Device::get>(0,"x","",ScalarType::F64,device);
#elif TELEMETRY_FACTORY_FAIL_CASE == 29
auto build(){float value=1;return makeField(0,"x","",[value]() noexcept {return value;});}
#elif TELEMETRY_FACTORY_FAIL_CASE == 30
constexpr auto target=static_cast<float (*)() noexcept>(nullptr);
constexpr auto bad=makeField(0,"x","",target);
#elif TELEMETRY_FACTORY_FAIL_CASE == 31
auto bad=makeField(0,"x","",[](){return 1.0f;});
#elif TELEMETRY_FACTORY_FAIL_CASE == 32
auto bad=makeField(0,"x","",parameterRead,parameterMismatch);
#elif TELEMETRY_FACTORY_FAIL_CASE == 33
auto bad=makeField(0,"x","",parameterRead,parameterThrowing);
#elif TELEMETRY_FACTORY_FAIL_CASE == 34
auto build(){float state=0;return makeField(0,"x","",parameterRead,
    [state](float) noexcept {return WriteResult::Applied;});}
#elif TELEMETRY_FACTORY_FAIL_CASE == 35
auto bad=makeCommand(0,"x",commandThrowing);
#elif TELEMETRY_FACTORY_FAIL_CASE == 36
auto build(){float state=0;return makeCommand(0,"x",
    [state](float) noexcept {return CommandResult::Executed;});}
#elif TELEMETRY_FACTORY_FAIL_CASE == 37
auto bad=makeCommand(0,"x",global,commandArgs(arg("v")));
#elif TELEMETRY_FACTORY_FAIL_CASE == 38
auto bad=makeField<&parameterRead>(0,"x","",parameterWrite);
#elif TELEMETRY_FACTORY_FAIL_CASE == 39
constexpr auto bad=makeField(0,"x","",parameterMode,parameterModeWrite);
#elif TELEMETRY_FACTORY_FAIL_CASE == 40
constexpr auto bad=makeCommand(0,"x",global);
#elif TELEMETRY_FACTORY_FAIL_CASE == 41
auto bad=makeCommand(0,"x",throwingCallable);
#elif TELEMETRY_FACTORY_FAIL_CASE == 42
auto bad=makeCommand(0,"x",genericCallable);
#elif TELEMETRY_FACTORY_FAIL_CASE == 43
auto bad=makeCommand(0,"x",overloadedCallable);
#elif TELEMETRY_FACTORY_FAIL_CASE == 44
auto bad=makeCommand(0,"x",stableCallable,commandArgs(arg("v")));
#elif TELEMETRY_FACTORY_FAIL_CASE == 45
constexpr auto bad=makeField<&Device::mode>(0,"x","",device,enumSpec<Other::Off,Other::On>());
#elif TELEMETRY_FACTORY_FAIL_CASE == 46
constexpr auto bad=enumSpec<Mode::Off,Other::On>();
#elif TELEMETRY_FACTORY_FAIL_CASE == 47
constexpr auto bad=enumSpec<Mode::Off,Mode::Off>();
#elif TELEMETRY_FACTORY_FAIL_CASE == 48
constexpr auto metadata=commandArgs(arg("mode","",enumSpec<Other::Off,Other::On>()));
constexpr auto bad=makeCommand<&Device::action>(0,"x",device,metadata);
#elif TELEMETRY_FACTORY_FAIL_CASE == 49
constexpr auto bad=makeField(0,"x","",parameterMode);
#elif TELEMETRY_FACTORY_FAIL_CASE == 50
auto bad=makeCommand(0,"x",[](float) noexcept {return CommandResult::Executed;});
#elif TELEMETRY_FACTORY_FAIL_CASE == 51
using CommandRows=Command[1];
auto bad=CommandCatalog{0,"temporary",CommandRows{},1};
#elif TELEMETRY_FACTORY_FAIL_CASE == 52
using CommandGroups=CommandCatalog[1];
auto bad=CommandCatalogIndex{CommandGroups{},1};
#elif TELEMETRY_FACTORY_FAIL_CASE == 53
constexpr auto duplicate=commandArgs(arg<0>("a"),arg<0>("b"));
constexpr auto bad=makeCommand<&Device::action>(0,"x",device,duplicate);
#elif TELEMETRY_FACTORY_FAIL_CASE == 54
constexpr auto outside=commandArgs(arg<1>("outside"));
constexpr auto bad=makeCommand<&Device::action>(0,"x",device,outside);
#elif TELEMETRY_FACTORY_FAIL_CASE == 55
constexpr auto wrongIndexed=commandArgs(arg<0>("v","",1));
constexpr auto bad=makeCommand<&Device::action>(0,"x",device,wrongIndexed);
#elif TELEMETRY_FACTORY_FAIL_CASE == 56
constexpr auto mixed=commandArgs(arg("positional"),arg<0>("indexed"));
constexpr auto bad=makeCommand<&Device::action>(0,"x",device,mixed);
#elif TELEMETRY_FACTORY_FAIL_CASE == 57
auto build(){float state=0;auto get=[&state]() noexcept{return state;};
    return makeField(0,"x","",get,[&state](float value) noexcept {
        state=value;return WriteResult::Applied;});}
#elif TELEMETRY_FACTORY_FAIL_CASE == 58
auto build(){float state=0;auto get=[&state]() noexcept{return state;};
    auto set=[&state](unsigned short value) noexcept {state=value;return WriteResult::Applied;};
    return makeField(0,"x","",get,set);}
#elif TELEMETRY_FACTORY_FAIL_CASE == 59
constexpr auto wrongEnum=commandArgs(arg<0>("mode","",Other::On));
constexpr auto bad=makeCommand<&Device::enumAction>(0,"x",device,wrongEnum);
#elif TELEMETRY_FACTORY_FAIL_CASE == 60
constexpr auto duplicateOrder=commandArgs(arg<1>("m"),arg<0>("v"),arg<1>("again"));
constexpr auto bad=makeCommand<&Device::pair>(0,"x",device,duplicateOrder);
#elif TELEMETRY_FACTORY_FAIL_CASE == 61
constexpr auto bad=CommandTable{command<&global>(0,"x",arg("value","",1.0f))};
#elif TELEMETRY_FACTORY_FAIL_CASE == 62
auto bad=CommandTable{command(0,"x",[](float) noexcept {return CommandResult::Executed;},
    arg<0>("value","",1.0f))};
#elif TELEMETRY_FACTORY_FAIL_CASE == 63
constexpr auto sourceTable=CommandTable{command<&global>(0,"x",arg<0>("value","",1.0f))};
auto bad=sourceTable;
#elif TELEMETRY_FACTORY_FAIL_CASE == 64
constexpr auto sourceCatalog=CommandCatalogTable{0,"group",
    command<&global>(makeId(0,0),"x",arg<0>("value","",1.0f))};
auto bad=sourceCatalog;
#elif TELEMETRY_FACTORY_FAIL_CASE == 65
auto build(){float state=0;auto get=[&state](){return state;};return makeField(0,"x","",get);}
#elif TELEMETRY_FACTORY_FAIL_CASE == 66
auto build(){float state=0;auto get=[&state]() noexcept{return state;};
    auto set=[&state](float value){state=value;return WriteResult::Applied;};
    return makeField(0,"x","",get,set);}
#elif TELEMETRY_FACTORY_FAIL_CASE == 67
auto build(){float state=0;auto get=[&state](auto...) noexcept{return state;};
    return makeField(0,"x","",get);}
#elif TELEMETRY_FACTORY_FAIL_CASE == 68
auto bad=makeField(0,"x","",overloadedFieldGetter);
#elif TELEMETRY_FACTORY_FAIL_CASE == 69
auto build(){float state=0;const auto get=[state]() mutable noexcept{return state;};
    return makeField(0,"x","",get);}
#elif TELEMETRY_FACTORY_FAIL_CASE == 70
auto build(){float state=0;auto get=[&state]() noexcept{return state;};
    return makeField(0,"x","",ScalarType::F32,get);}
#elif TELEMETRY_FACTORY_FAIL_CASE == 71
auto bad=CommandTable{command<&global>(0,"x",arg<0>("value","",1.0f))}.index();
#elif TELEMETRY_FACTORY_FAIL_CASE == 72
auto bad=CommandTable{command<&global>(0,"x",arg<0>("value","",1.0f))}.data();
#elif TELEMETRY_FACTORY_FAIL_CASE == 73
auto bad=CommandTable{command<&global>(0,"x",arg<0>("value","",1.0f))}[0];
#elif TELEMETRY_FACTORY_FAIL_CASE == 74
auto bad=CommandCatalogTable{0,"group",
    command<&global>(makeId(0,0),"x",arg<0>("value","",1.0f))}.catalog();
#elif TELEMETRY_FACTORY_FAIL_CASE == 75
auto bad=CommandCatalogTable{0,"group",
    command<&global>(makeId(0,0),"x",arg<0>("value","",1.0f))}.index();
#elif TELEMETRY_FACTORY_FAIL_CASE == 76
auto bad=CommandCatalogTable{0,"group",
    command<&global>(makeId(0,0),"x",arg<0>("value","",1.0f))}.data();
#else
#error Unknown factory case
#endif
int main() {}
