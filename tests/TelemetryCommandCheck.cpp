// Commands validate arguments before owner side effects and export typed schema.
#include "Telemetry.h"
#include "serialization/TelemetryCommandJson.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>

namespace {
constexpr telemetry::CommandTable emptyCommandTable{};
constexpr telemetry::CommandCatalogTable emptyCommandCatalog{0, "empty"};
static_assert(emptyCommandTable.size() == 0);
static_assert(emptyCommandTable.index().find(0) == nullptr);
static_assert(emptyCommandCatalog.size() == 0);
static_assert(emptyCommandCatalog.catalog().count == 0);
static_assert(telemetry::CommandTable{}.size() == 0);
static_assert(telemetry::CommandCatalogTable{0, "empty"}.size() == 0);

using namespace telemetry;
int checks = 0, failures = 0;
void expect(bool ok, const char* label) { ++checks; if (!ok) {++failures; std::printf("FAIL %s\n", label);} }
enum class Mode : std::uint8_t { Fast, Normal, Precise };
enum class Error : std::uint16_t { None = 0, Overvoltage = 1000, Overcurrent = 2000 };
enum Legacy { Low = -2, High = 2 };
struct Device {
    int calls = 0;
    float voltage = 0;
    Mode mode = Mode::Fast;
    std::uint64_t address = 0;
    Error error = Error::None;
    CommandResult reset() noexcept { ++calls; voltage=0; return CommandResult::Executed; }
    CommandResult calibrate(float v,Mode m) noexcept { ++calls; voltage=v; mode=m; return CommandResult::Executed; }
    CommandResult setAddress(std::uint64_t value) noexcept { ++calls; address=value; return CommandResult::Accepted; }
    CommandResult diagnostics() const noexcept { return CommandResult::Busy; }
    CommandResult legacy(Legacy) noexcept { ++calls; return CommandResult::Executed; }
    CommandResult clearError(Error value) noexcept { ++calls; error = value; return CommandResult::Executed; }
};
Device device;
int freeCalls=0;
int manyCalls=0;
CommandResult numbers(float f,double d,std::uint8_t u8,std::uint16_t u16,std::uint32_t u32,
                      std::uint64_t u64,std::int8_t s8,std::int16_t s16,std::int32_t s32,
                      std::int64_t s64,bool flag) noexcept
{
    ++manyCalls;
    return f==1.25f && d==-2.5 && u8==UINT8_MAX && u16==UINT16_MAX && u32==UINT32_MAX
        && u64==UINT64_MAX && s8==INT8_MIN && s16==INT16_MIN && s32==INT32_MIN
        && s64==INT64_MIN && flag ? CommandResult::Executed : CommandResult::Failed;
}
constexpr auto many=makeCommand<&numbers>(0,"numbers");
CommandResult freeCommand(std::int64_t, bool) noexcept { ++freeCalls; return CommandResult::Failed; }
struct System { static CommandResult save() noexcept { return CommandResult::Executed; } };
constexpr auto lambdaTarget = +[](std::uint16_t value) noexcept {
    return value==12 ? CommandResult::Executed : CommandResult::Failed;
};
constexpr auto lambdaCommand = makeCommand<lambdaTarget>(0,"lambda");
constexpr auto calibrateArgs = commandArgs(arg("Voltage", "V", 230.0f, 0.0f, 500.0f),arg("Mode", "", Mode::Normal));
constexpr auto addressArgs = commandArgs(arg("Address", "", UINT64_MAX, UINT64_C(1), UINT64_MAX));
constexpr auto errorArgs = commandArgs(arg("Error", "",
    enumSpec<Error::None, Error::Overvoltage, Error::Overcurrent>(Error::Overvoltage)));
constexpr auto partialCalibrateArgs = commandArgs(
    arg<1>("Mode only", "", enumSpec<Mode::Fast, Mode::Normal, Mode::Precise>(Mode::Normal)));
constexpr auto reorderedCalibrateArgs = commandArgs(
    arg<1>("Mode reordered", "", Mode::Precise),
    arg<0>("Voltage reordered", "V", 230.0f, 0.0f, 500.0f));
constexpr auto partialCalibrate = makeCommand<&Device::calibrate>(
    0, "Partial", device, partialCalibrateArgs);
constexpr auto reorderedCalibrate = makeCommand<&Device::calibrate>(
    1, "Reordered", device, reorderedCalibrateArgs);
constexpr CommandTable ownedCommands{
    command<&Device::reset>(0, "Owned reset", device),
    command<&Device::calibrate>(
        1, "Owned calibrate", device,
        arg<1>("Owned mode", "",
               enumSpec<Mode::Fast, Mode::Normal, Mode::Precise>(Mode::Normal)),
        arg<0>("Owned voltage", "V", 230.0f, 0.0f, 500.0f))};
constexpr CommandCatalogTable ownedApi{
    0, "Owned/Motor",
    command<&Device::reset>(makeId(0, 0), "Catalog reset", device),
    command<&Device::calibrate>(
        makeId(0, 1), "Catalog calibrate", device,
        arg<0>("Catalog voltage", "V", 230.0f, 0.0f, 500.0f),
        arg<1>("Catalog mode", "", Mode::Normal))};
constexpr CommandCatalog ownedCatalogViews[] = {ownedApi.catalog()};
constexpr CommandCatalogIndex ownedApiIndex{ownedCatalogViews};
constexpr Command commands[] = {
    makeCommand<&Device::reset>(0,"Reset",device),
    makeCommand<&Device::calibrate>(1,"Calibrate",device,calibrateArgs),
    makeCommand<&Device::setAddress>(2,"Address",device,addressArgs),
    makeCommand<&freeCommand>(3,"Free"),
    makeCommand<&System::save>(4,"Save"),
    makeCommand<&Device::calibrate>(5,"Automatic",device),
    makeCommand<&Device::legacy>(6,"Legacy",device),
    makeCommand<&Device::clearError>(7,"ClearError",device,errorArgs),
};
constexpr CommandIndex commandsIndex{commands};
constexpr Command motorCommands[] = {
    makeCommand<&System::save>(makeId(1, 0), "Tune"),
};
constexpr CommandCatalog commandCatalogs[] = {
    {0, "System", commands},
    {1, "Motor/Control", motorCommands},
};
constexpr CommandCatalogIndex groupedCommands{commandCatalogs};
static_assert(commandsIndex.size()==8 && commands[0].metadata==nullptr);
static_assert(groupedCommands.size()==2
              && groupedCommands.find(makeId(1, 0)) == &motorCommands[0]);
static_assert(commandNamesUnique(commands, std::size(commands))
              && commandCatalogNamesUnique(commandCatalogs, std::size(commandCatalogs)));
static_assert(!std::is_copy_assignable_v<Command> && std::is_trivially_copyable_v<Command>);
static_assert(sizeof(Command)==sizeof(void*)*6);
static_assert(ownedCommands.size() == 2
              && ownedCommands.index().find(1) == &ownedCommands[1]
              && ownedCommands[0].metadata == nullptr);
static_assert(!std::is_copy_constructible_v<std::remove_cv_t<decltype(ownedCommands)>>
              && !std::is_move_constructible_v<std::remove_cv_t<decltype(ownedCommands)>>
              && !std::is_trivially_copyable_v<std::remove_cv_t<decltype(ownedCommands)>>);
static_assert(ownedApi.size() == 2
              && ownedApiIndex.find(makeId(0, 1)) == &ownedApi.data()[1]
              && !std::is_copy_constructible_v<std::remove_cv_t<decltype(ownedApi)>>
              && !std::is_move_constructible_v<std::remove_cv_t<decltype(ownedApi)>>
              && !std::is_trivially_copyable_v<std::remove_cv_t<decltype(ownedApi)>>);
bool stop(void* state,const CommandParam&) noexcept { ++*static_cast<int*>(state); return false; }
template <class Index>
bool boundaries(const Index& view,JsonOptions options)
{
    std::array<char,4096> reference{};
    const auto len=writeSchema(view,reference.data(),reference.size(),options);
    if(len==0 || len+2>reference.size()) return false;
    for(std::size_t size=0;size<=len+2;++size){
        std::array<char,4100> buffer; buffer.fill('#');
        const auto written=writeSchema(view,buffer.data()+2,size,options);
        if(written!=(size>len?len:0) || buffer[0]!='#' || buffer[1]!='#')return false;
        for(std::size_t p=size+2;p<buffer.size();++p)if(buffer[p]!='#')return false;
        if(size && std::memchr(buffer.data()+2,0,size)==nullptr)return false;
        if(written && std::strcmp(buffer.data()+2,reference.data()))return false;
    }
    return true;
}
}
int main()
{
    expect(commandsIndex.call(0)==CommandResult::Executed && device.calls==1,"zero-argument method");
    expect(commandsIndex.call(1,250,Mode::Precise)==CommandResult::Executed && device.voltage==250.0f && device.mode==Mode::Precise,"typed member conversion");
    const auto before=device.calls;
    expect(commandsIndex.call(1,250)==CommandResult::ArgumentCountMismatch && device.calls==before,"missing argument has no side effect");
    expect(commandsIndex.call(1,250,1,7)==CommandResult::ArgumentCountMismatch,"excess arguments");
    expect(commandsIndex.call(1,501.0f,1)==CommandResult::InvalidValue && device.calls==before,"custom limit");
    expect(commandsIndex.call(1,250,99)==CommandResult::InvalidValue && device.calls==before,"last invalid arg prevents whole command");
    expect(commandsIndex.call(1,Scalar{},1)==CommandResult::InvalidValue,"null arg rejected");
    expect(commandsIndex.execute(1,nullptr,2)==CommandResult::InvalidValue,"null pointer with matching count");
    expect(commandsIndex.execute(1,nullptr,0)==CommandResult::ArgumentCountMismatch,"count checked first");
    expect(commandsIndex.call(100)==CommandResult::NotFound,"command ID out of bounds");
    expect(Command{}.call()==CommandResult::Unavailable,"empty command safe");
    expect(commandsIndex.call(2,UINT64_MAX)==CommandResult::Accepted && device.address==UINT64_MAX,"U64 exact and Accepted forwarded");
    expect(commandsIndex.call(2,-1)==CommandResult::InvalidValue,"negative to U64 rejected");
    expect(commandsIndex.call(2,0)==CommandResult::InvalidValue,"U64 lower bound");
    expect(commandsIndex.call(3,INT64_MIN,true)==CommandResult::Failed && freeCalls==1,"free function return preserved");
    expect(commandsIndex.call(4)==CommandResult::Executed,"static method without owner");
    expect(many.call(1.25f,-2.5,UINT8_MAX,UINT16_MAX,UINT32_MAX,UINT64_MAX,INT8_MIN,
                    INT16_MIN,INT32_MIN,INT64_MIN,true)==CommandResult::Executed && manyCalls==1,
           "all eleven scalar types and more than eight arguments");
    expect(many.call(1.25f,-2.5,256,UINT16_MAX,UINT32_MAX,UINT64_MAX,INT8_MIN,
                    INT16_MIN,INT32_MIN,INT64_MIN,true)==CommandResult::InvalidValue && manyCalls==1,
           "interior argument overflow prevents wide signature invocation");
    expect(lambdaCommand.call(12.9)==CommandResult::Executed,"named C++17 lambda with truncation");
    Device borrowedDevice;
    auto capturing = [&borrowedDevice](float value, Mode next) noexcept {
        return borrowedDevice.calibrate(value, next);
    };
    const auto borrowedLambda = makeCommand(0,"borrowed lambda",capturing,calibrateArgs);
    expect(borrowedLambda.call(300,Mode::Normal)==CommandResult::Executed
           && borrowedDevice.voltage==300.0f && borrowedDevice.mode==Mode::Normal,
           "stable capturing lambda is borrowed");
    expect(borrowedLambda.call(501,Mode::Fast)==CommandResult::InvalidValue
           && borrowedDevice.calls==1,"borrowed callable validates before invocation");
    struct Stateful {
        Device& owner;
        CommandResult operator()(std::uint16_t value) noexcept
        { owner.address=value; return CommandResult::Accepted; }
    } stateful{borrowedDevice};
    auto borrowedFunctor = makeCommand(0,"borrowed functor",stateful);
    expect(borrowedFunctor.call(42.9)==CommandResult::Accepted
           && borrowedDevice.address==42,"stable stateful functor and conversion");
    const auto statelessTarget = [](bool value) noexcept {
        return value ? CommandResult::Executed : CommandResult::Failed;
    };
    constexpr auto boolArgs = commandArgs(arg("Enabled"));
    const auto borrowedConst = makeCommand(0,"borrowed const",statelessTarget,boolArgs);
    expect(borrowedConst.call(1)==CommandResult::Executed,
           "const named callable lvalue is borrowed");
    expect(partialCalibrate.call(700.0f, Mode::Precise) == CommandResult::Executed
           && device.voltage == 700.0f && device.mode == Mode::Precise,
           "indexed partial metadata infers omitted argument");
    const auto partialCalls = device.calls;
    expect(partialCalibrate.call(700.0f, 3) == CommandResult::InvalidValue
           && device.calls == partialCalls,
           "indexed enum metadata validates its selected argument");
    expect(reorderedCalibrate.call(500.0f, Mode::Fast) == CommandResult::Executed
           && reorderedCalibrate.call(501.0f, Mode::Fast) == CommandResult::InvalidValue,
           "indexed metadata order is independent of signature order");
    expect(ownedCommands.index().call(0) == CommandResult::Executed
           && ownedCommands.index().call(1, 300.0f, Mode::Normal) == CommandResult::Executed
           && device.voltage == 300.0f && device.mode == Mode::Normal,
           "owning command table executes ordinary Command views");
    expect(ownedApiIndex.call(makeId(0, 1), 320.0f, Mode::Precise)
               == CommandResult::Executed
           && device.voltage == 320.0f && device.mode == Mode::Precise,
           "owning command catalog exposes the ordinary grouped index");
    auto ownedCallable = [&borrowedDevice](std::uint16_t value) noexcept {
        borrowedDevice.address = value;
        return CommandResult::Accepted;
    };
    const auto borrowedTable = CommandTable{
        command(0, "Owned callable", ownedCallable,
                arg<0>("Address", "", std::uint16_t{7}, std::uint16_t{1}, std::uint16_t{100}))};
    expect(borrowedTable.index().call(0, 42) == CommandResult::Accepted
           && borrowedDevice.address == 42,
           "owning table borrows a stable capturing command callable");
    expect(commandsIndex.call(5,-50.0,2)==CommandResult::Executed && device.voltage==-50.0f,"inferred native bounds");
    expect(commandsIndex.call(5,std::numeric_limits<float>::infinity(),1)==CommandResult::InvalidValue,"infinite native input rejected");
    expect(commandsIndex.call(5,std::numeric_limits<float>::quiet_NaN(),1)==CommandResult::InvalidValue,"nan native input rejected");
    expect(commandsIndex.call(6,0)==CommandResult::Executed,"unfixed enum gap allowed");
    expect(commandsIndex.call(6,1000)==CommandResult::InvalidValue,"unfixed enum cast guarded");
    expect(commandsIndex.call(7,2000)==CommandResult::Executed
           && device.error==Error::Overcurrent,"explicit sparse command enum accepted");
    expect(commandsIndex.call(7,2001)==CommandResult::InvalidValue,
           "explicit sparse command enum bounds enforced");
    expect(groupedCommands.call(makeId(1,0))==CommandResult::Executed,
           "grouped command direct lookup");
    expect(groupedCommands.call(makeId(2,0))==CommandResult::NotFound,
           "grouped command missing group");
    const Command brokenRows[]={
        makeCommand<&System::save>(makeId(0,0),"First"),
        makeCommand<&System::save>(makeId(0,2),"Gap"),
        makeCommand<&System::save>(makeId(0,1),"Past gap"),
    };
    const CommandCatalog preservedGroups[]={
        {0,"Broken rows",brokenRows},
        {1,"Motor",motorCommands},
    };
    const CommandCatalogIndex preserved{preservedGroups};
    expect(preserved.size()==2 && preserved.data()[0].count==1
           && preserved.find(makeId(0,1))==nullptr
           && preserved.find(makeId(1,0))==&motorCommands[0],
           "row gap trims one command group without hiding the next group");
    const Command groupTwo[]={makeCommand<&System::save>(makeId(2,0),"Late")};
    const CommandCatalog brokenGroups[]={
        {0,"System",commands},
        {2,"Past group gap",groupTwo},
    };
    const CommandCatalogIndex clipped{brokenGroups};
    expect(clipped.size()==1 && clipped.find(makeId(2,0))==nullptr,
           "group gap truncates the grouped command prefix");
    expect(CommandCatalog{}.count==0 && CommandCatalog{0,"null",nullptr,100}.count==0
           && CommandCatalogIndex{}.find(0)==nullptr
           && CommandCatalogIndex{nullptr,100}.size()==0,
           "default and null grouped command definitions are empty");
    const auto preservedCopy=preserved;
    expect(preservedCopy.catalog(1)==&preservedGroups[1]
           && preservedCopy.catalog(2)==nullptr
           && preservedCopy.find(makeId(1,0))==&motorCommands[0],
           "copied grouped command view retains original definitions");
    const Command duplicateCommands[]={
        makeCommand<&System::save>(0,"same"),
        makeCommand<&System::save>(1,"same"),
    };
    const Command nullCommandNames[]={makeCommand<&System::save>(0,nullptr)};
    const CommandCatalog duplicateCatalogNames[]={
        {0,"same",commands},
        {1,"same",motorCommands},
    };
    const CommandCatalog nullCatalogNames[]={{0,nullptr,commands}};
    expect(!commandNamesUnique(duplicateCommands,std::size(duplicateCommands))
           && !commandNamesUnique(nullCommandNames,std::size(nullCommandNames))
           && !commandCatalogNamesUnique(duplicateCatalogNames,std::size(duplicateCatalogNames))
           && !commandCatalogNamesUnique(nullCatalogNames,std::size(nullCatalogNames)),
           "grouped command name helpers reject duplicate and null names");
    Device local;
    const auto runtime=makeCommand<&Device::calibrate>(0,"local",local,calibrateArgs);
    const auto copy=runtime;
    expect(copy.call(10.0f,1)==CommandResult::Executed && local.calls==1,"copy borrows runtime owner");
    const Device constant;
    const auto diagnostics=makeCommand<&Device::diagnostics>(0,"diag",constant);
    expect(diagnostics.call()==CommandResult::Busy,"const owner method");
    int described=0;
    expect(!commands[1].describeParameters(&described,&stop) && described==1,"schema sink can stop");
    expect(!commands[1].describeParameters(nullptr,nullptr),"null sink safe");
    const Command gaps[]={commands[0],commands[2],commands[1]};
    const CommandIndex prefix{gaps};
    expect(prefix.size()==1 && !prefix.find(1),"dense command prefix");
    expect(CommandIndex{nullptr,100}.size()==0,"null table empty");
    std::array<char,4096> json{},strings{};
    const auto calls=device.calls;
    const auto n=writeSchema(commandsIndex,json.data(),json.size());
    expect(n && device.calls==calls,"schema never invokes command");
    expect(std::strstr(json.data(),"\"id\":0,\"n\":\"Reset\",\"params\":[]")!=nullptr,"zero params schema");
    expect(std::strstr(json.data(),"\"i\":0,\"n\":\"Voltage\",\"u\":\"V\",\"t\":\"f32\",\"min\":0,\"max\":500,\"default\":230")!=nullptr,"numeric decorations");
    expect(std::strstr(json.data(),"\"t\":\"u8\",\"min\":0,\"max\":2,\"default\":1,\"enum\":{\"0\":\"Fast\",\"1\":\"Normal\",\"2\":\"Precise\"}")!=nullptr,"enum inferred dictionary and default");
    expect(std::strstr(json.data(),"\"t\":\"u16\",\"min\":0,\"max\":2000,\"default\":1000,\"enum\":{\"0\":\"None\",\"1000\":\"Overvoltage\",\"2000\":\"Overcurrent\"}")!=nullptr,
           "explicit sparse enum dictionary in command schema");
    expect(std::strstr(json.data(),"\"i\":0,\"t\":\"f32\",\"min\":null,\"max\":null,\"default\":0")!=nullptr,"metadata-less schema");
    const Command indexedRows[] = {partialCalibrate, reorderedCalibrate};
    std::array<char,2048> indexedJson{};
    expect(writeSchema(CommandIndex{indexedRows}, indexedJson.data(), indexedJson.size()) != 0
           && std::strstr(indexedJson.data(),
               "\"i\":0,\"t\":\"f32\",\"min\":null,\"max\":null,\"default\":0") != nullptr
           && std::strstr(indexedJson.data(),
               "\"i\":1,\"n\":\"Mode only\",\"u\":\"\",\"t\":\"u8\"") != nullptr
           && std::strstr(indexedJson.data(),
               "\"i\":0,\"n\":\"Voltage reordered\",\"u\":\"V\"") != nullptr,
           "partial and reordered metadata serialize in signature order");
    std::array<char,2048> ownedJson{};
    expect(writeSchema(ownedCommands.index(), ownedJson.data(), ownedJson.size()) != 0
           && std::strstr(ownedJson.data(), "\"n\":\"Owned voltage\"") != nullptr
           && std::strstr(ownedJson.data(), "\"n\":\"Owned mode\"") != nullptr,
           "owning command table serializes owned inline metadata");
    expect(writeSchema(ownedApiIndex, ownedJson.data(), ownedJson.size()) != 0
           && std::strstr(ownedJson.data(), "\"name\":\"Owned/Motor\"") != nullptr
           && std::strstr(ownedJson.data(), "\"n\":\"Catalog voltage\"") != nullptr,
           "owning command catalog serializes through CommandCatalogIndex");
    expect(writeSchema(commandsIndex,strings.data(),strings.size(),{JsonInt64Mode::String})!=0
           && std::strstr(strings.data(),"\"min\":\"1\",\"max\":null,\"default\":\"18446744073709551615\"")!=nullptr,"command schema U64 string mode");
    expect(std::strncmp(json.data(),strings.data(),20)==0,"CRC independent of representation");
    expect(boundaries(commandsIndex,{}) && boundaries(commandsIndex,{JsonInt64Mode::String}),"all schema buffer boundaries");
    expect(boundaries(groupedCommands,{}) && boundaries(groupedCommands,{JsonInt64Mode::String}),
           "all grouped command schema buffer boundaries");
    std::array<char,4096> groupedJson{};
    expect(writeSchema(groupedCommands,groupedJson.data(),groupedJson.size())!=0
           && std::strstr(groupedJson.data(),"\"commandCatalogs\":[{\"id\":0,\"name\":\"System\"")!=nullptr
           && std::strstr(groupedJson.data(),"\"id\":1,\"name\":\"Motor/Control\"")!=nullptr
           && std::strstr(groupedJson.data(),"\"i\":0,\"id\":65536,\"n\":\"Tune\"")!=nullptr,
           "grouped command hierarchy schema");
    expect(writeSchema(commandsIndex,nullptr,0)==0 && writeSchema(commandsIndex,nullptr,4096)==0,"null buffers");
    const Command nullNames[]={makeCommand<&System::save>(0,nullptr)};
    expect(writeSchema(CommandIndex{nullNames},json.data(),json.size())==0 && schemaCrc(CommandIndex{nullNames})==0,"null command name");
    const Command escaped[]={makeCommand<&System::save>(0,"A\"B\n")};
    expect(writeSchema(CommandIndex{escaped},json.data(),json.size())!=0 && std::strstr(json.data(),"A\\\"B\\u000a")!=nullptr,"escaped names");
    expect(schemaCrc(CommandIndex{})!=schemaCrc(commandsIndex),"nonempty fingerprint");
    std::printf("%d/%d command checks passed\n",checks-failures,checks);
    return failures!=0;
}
