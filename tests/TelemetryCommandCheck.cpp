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
using namespace telemetry;
int checks = 0, failures = 0;
void expect(bool ok, const char* label) { ++checks; if (!ok) {++failures; std::printf("FAIL %s\n", label);} }
enum class Mode : std::uint8_t { Fast, Normal, Precise };
enum Legacy { Low = -2, High = 2 };
struct Device {
    int calls = 0;
    float voltage = 0;
    Mode mode = Mode::Fast;
    std::uint64_t address = 0;
    CommandResult reset() noexcept { ++calls; voltage=0; return CommandResult::Executed; }
    CommandResult calibrate(float v,Mode m) noexcept { ++calls; voltage=v; mode=m; return CommandResult::Executed; }
    CommandResult setAddress(std::uint64_t value) noexcept { ++calls; address=value; return CommandResult::Accepted; }
    CommandResult diagnostics() const noexcept { return CommandResult::Busy; }
    CommandResult legacy(Legacy) noexcept { ++calls; return CommandResult::Executed; }
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
constexpr Command commands[] = {
    makeCommand<&Device::reset>(0,"Reset",device),
    makeCommand<&Device::calibrate>(1,"Calibrate",device,calibrateArgs),
    makeCommand<&Device::setAddress>(2,"Address",device,addressArgs),
    makeCommand<&freeCommand>(3,"Free"),
    makeCommand<&System::save>(4,"Save"),
    makeCommand<&Device::calibrate>(5,"Automatic",device),
    makeCommand<&Device::legacy>(6,"Legacy",device),
};
constexpr CommandIndex commandsIndex{commands};
static_assert(commandsIndex.size()==7 && commands[0].metadata==nullptr);
static_assert(!std::is_copy_assignable_v<Command> && std::is_trivially_copyable_v<Command>);
static_assert(sizeof(Command)==sizeof(void*)*6);
bool stop(void* state,const CommandParam&) noexcept { ++*static_cast<int*>(state); return false; }
bool boundaries(const CommandIndex& view,JsonOptions options)
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
    expect(commandsIndex.call(5,-50.0,2)==CommandResult::Executed && device.voltage==-50.0f,"inferred native bounds");
    expect(commandsIndex.call(5,std::numeric_limits<float>::infinity(),1)==CommandResult::InvalidValue,"infinite native input rejected");
    expect(commandsIndex.call(5,std::numeric_limits<float>::quiet_NaN(),1)==CommandResult::InvalidValue,"nan native input rejected");
    expect(commandsIndex.call(6,0)==CommandResult::Executed,"unfixed enum gap allowed");
    expect(commandsIndex.call(6,1000)==CommandResult::InvalidValue,"unfixed enum cast guarded");
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
    expect(std::strstr(json.data(),"\"i\":0,\"t\":\"f32\",\"min\":null,\"max\":null,\"default\":0")!=nullptr,"metadata-less schema");
    expect(writeSchema(commandsIndex,strings.data(),strings.size(),{JsonInt64Mode::String})!=0
           && std::strstr(strings.data(),"\"min\":\"1\",\"max\":null,\"default\":\"18446744073709551615\"")!=nullptr,"command schema U64 string mode");
    expect(std::strncmp(json.data(),strings.data(),20)==0,"CRC independent of representation");
    expect(boundaries(commandsIndex,{}) && boundaries(commandsIndex,{JsonInt64Mode::String}),"all schema buffer boundaries");
    expect(writeSchema(commandsIndex,nullptr,0)==0 && writeSchema(commandsIndex,nullptr,4096)==0,"null buffers");
    const Command nullNames[]={makeCommand<&System::save>(0,nullptr)};
    expect(writeSchema(CommandIndex{nullNames},json.data(),json.size())==0 && schemaCrc(CommandIndex{nullNames})==0,"null command name");
    const Command escaped[]={makeCommand<&System::save>(0,"A\"B\n")};
    expect(writeSchema(CommandIndex{escaped},json.data(),json.size())!=0 && std::strstr(json.data(),"A\\\"B\\u000a")!=nullptr,"escaped names");
    expect(schemaCrc(CommandIndex{})!=schemaCrc(commandsIndex),"nonempty fingerprint");
    std::printf("%d/%d command checks passed\n",checks-failures,checks);
    return failures!=0;
}
