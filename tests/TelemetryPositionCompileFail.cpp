// Named positions must not bypass type, sign or full-width bounds checks.
// Authors: Ruslan Kovtun (shpegun60), codexAi. License: MIT.
#include "Telemetry.h"
#include <limits>
using namespace telemetry;

float readNumber() noexcept {return 1;}
WriteResult writeNumber(float) noexcept {return WriteResult::Applied;}
CommandResult action() noexcept {return CommandResult::Executed;}
constexpr FieldTable rows{field<&readNumber,&writeNumber>("value","")};
constexpr CommandTable commands{command<&action>("action")};
enum class SignedPosition : std::int64_t {Negative=-1, PastEnd=1};
enum class WidePosition : std::uint64_t {
    WrapsOnArm=UINT64_C(0x100000000), Maximum=UINT64_MAX
};
int object=0;

#if TELEMETRY_POSITION_FAIL_CASE == 1
auto rejected=rows.read<SignedPosition::Negative>();
#elif TELEMETRY_POSITION_FAIL_CASE == 2
auto rejected=rows.read<SignedPosition::Negative,double>();
#elif TELEMETRY_POSITION_FAIL_CASE == 3
auto rejected=rows.write<SignedPosition::Negative>(1);
#elif TELEMETRY_POSITION_FAIL_CASE == 4
auto rejected=commands.call<SignedPosition::Negative>();
#elif TELEMETRY_POSITION_FAIL_CASE == 5
auto rejected=rows.read<SignedPosition::PastEnd>();
#elif TELEMETRY_POSITION_FAIL_CASE == 6
auto rejected=rows.read<SignedPosition::PastEnd,double>();
#elif TELEMETRY_POSITION_FAIL_CASE == 7
auto rejected=rows.write<SignedPosition::PastEnd>(1);
#elif TELEMETRY_POSITION_FAIL_CASE == 8
auto rejected=commands.call<SignedPosition::PastEnd>();
#elif TELEMETRY_POSITION_FAIL_CASE == 9
auto rejected=rows.read<WidePosition::WrapsOnArm>();
#elif TELEMETRY_POSITION_FAIL_CASE == 10
auto rejected=commands.call<WidePosition::WrapsOnArm>();
#elif TELEMETRY_POSITION_FAIL_CASE == 11
auto rejected=rows.write<WidePosition::Maximum>(1);
#elif TELEMETRY_POSITION_FAIL_CASE == 12
auto rejected=commands.call<WidePosition::Maximum>();
#elif TELEMETRY_POSITION_FAIL_CASE == 13
auto rejected=rows.read<-1>();
#elif TELEMETRY_POSITION_FAIL_CASE == 14
auto rejected=commands.call<-1>();
#elif TELEMETRY_POSITION_FAIL_CASE == 15
auto rejected=rows.read<&object>();
#elif TELEMETRY_POSITION_FAIL_CASE == 16
auto rejected=commands.call<&object>();
#elif TELEMETRY_POSITION_FAIL_CASE == 17
auto rejected=rows.read<nullptr>();
#elif TELEMETRY_POSITION_FAIL_CASE == 18
auto rejected=commands.call<nullptr>();
#elif TELEMETRY_POSITION_FAIL_CASE == 19 || TELEMETRY_POSITION_FAIL_CASE == 20
struct ConvertiblePosition {constexpr operator std::size_t() const noexcept {return 0;}};
#if TELEMETRY_POSITION_FAIL_CASE == 19
auto rejected=rows.read<ConvertiblePosition{}>();
#else
auto rejected=commands.call<ConvertiblePosition{}>();
#endif
#elif TELEMETRY_POSITION_FAIL_CASE == 21
auto rejected=rows.read<0.0f>();
#elif TELEMETRY_POSITION_FAIL_CASE == 22
auto rejected=commands.call<0.0>();
#else
#error Select TELEMETRY_POSITION_FAIL_CASE from 1 through 22
#endif
