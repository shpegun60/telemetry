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
#else
#error Unknown factory case
#endif
int main() {}
