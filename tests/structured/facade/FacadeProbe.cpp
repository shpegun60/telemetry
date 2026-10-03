/*
 * @file FacadeProbe.cpp
 * @brief Stable aggregate and callable facts without exposing vendor APIs.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */

#include "../reflection/ProbeTypes.hpp"

#include <telemetry/reflection/Reflection.hpp>
#include <telemetry/result/EndpointStatus.hpp>
#include <telemetry/result/EndpointStatus.hpp>

#include <cstdint>
#include <functional>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

namespace refl = telemetry::reflection;
using telemetry_structured_probe::MeterConfig;
using telemetry_structured_probe::Nested;
using telemetry_structured_probe::RepeatedTypes;

namespace facade_probe {

struct Request { std::uint16_t index; };
struct Response { float voltage; };
struct ConstMember { const int code; };

Response read(const Request&) noexcept;
int bare(int) noexcept;

struct Owner {
    static int staticRead(int) noexcept;
    Response read(const Request&) const & noexcept;
    int update(int) & noexcept;
    int observed() volatile noexcept;
    int temporary() && noexcept;
    int qualified() const volatile && noexcept;
    int throwing() noexcept(false);
    int variadic(int, ...) noexcept;
    const Response& reference() const noexcept;
};

struct Overloaded {
    int operator()(int) const noexcept;
    int operator()(float) const noexcept;
};

struct NoConstruct {
    NoConstruct() = delete;
    int read() const noexcept;
};

} // namespace facade_probe

static_assert(refl::memberCount<MeterConfig> == 2);
static_assert(refl::memberCount<const RepeatedTypes> == 2);
static_assert(refl::memberCount<Nested> == 2);
static_assert(refl::memberName<0, MeterConfig>() == "voltage");
static_assert(refl::memberName<1, MeterConfig>() == "rpm");
static_assert(refl::memberName<0, RepeatedTypes>() == "phaseA");
static_assert(refl::memberName<1, RepeatedTypes>() == "phaseB");
static_assert(std::is_same_v<refl::MemberType<0, MeterConfig>, float>);
static_assert(std::is_same_v<refl::MemberType<1, MeterConfig>, std::uint16_t>);
static_assert(std::is_same_v<refl::MemberType<1, Nested>, std::array<std::uint16_t, 3>>);
static_assert(std::is_same_v<refl::MemberType<0, facade_probe::ConstMember>, const int>);
static_assert(std::is_same_v<decltype(refl::get<0>(std::declval<MeterConfig&>())), float&>);
static_assert(std::is_same_v<decltype(refl::get<0>(std::declval<const MeterConfig&>())), const float&>);
static_assert(refl::automaticNameValid("_field9"));
static_assert(!refl::automaticNameValid("9field"));
static_assert(!refl::automaticNameValid("\xCE\x94Limit"));
static_assert(std::string_view{"\xCE\x94Limit"}.size() == 7);

using Free = refl::Function<decltype(&facade_probe::read)>;
static_assert(std::is_same_v<Free::Result, facade_probe::Response>);
static_assert(std::is_same_v<Free::Arguments, std::tuple<const facade_probe::Request&>>);
static_assert(std::is_same_v<Free::Owner, void>);
static_assert(Free::arity == 1 && !Free::isMember && Free::isNoexcept);

using Method = refl::Function<decltype(&facade_probe::Owner::read)>;
static_assert(std::is_same_v<Method::Owner, facade_probe::Owner>);
static_assert(Method::isMember && Method::isConst && !Method::isVolatile);
static_assert(Method::refQualifier == refl::RefQualifier::LValue);
static_assert(Method::isNoexcept && !Method::isVariadic);

using Mutable = refl::Function<decltype(&facade_probe::Owner::update)>;
static_assert(!Mutable::isConst && Mutable::refQualifier == refl::RefQualifier::LValue);
using Static = refl::Function<decltype(&facade_probe::Owner::staticRead)>;
static_assert(!Static::isMember && Static::isNoexcept);
using Volatile = refl::Function<decltype(&facade_probe::Owner::observed)>;
static_assert(Volatile::isVolatile);
using RValue = refl::Function<decltype(&facade_probe::Owner::temporary)>;
static_assert(RValue::refQualifier == refl::RefQualifier::RValue);
using Combined = refl::Function<decltype(&facade_probe::Owner::qualified)>;
static_assert(Combined::isConst && Combined::isVolatile
              && Combined::refQualifier == refl::RefQualifier::RValue);
using Throwing = refl::Function<decltype(&facade_probe::Owner::throwing)>;
static_assert(!Throwing::isNoexcept);
using Variadic = refl::Function<decltype(&facade_probe::Owner::variadic)>;
static_assert(Variadic::isVariadic);
using Reference = refl::Function<decltype(&facade_probe::Owner::reference)>;
static_assert(std::is_same_v<Reference::Result, const facade_probe::Response&>);
using NoConstruct = refl::Function<decltype(&facade_probe::NoConstruct::read)>;
static_assert(NoConstruct::arity == 0 && NoConstruct::isConst);

using BareLambda = refl::Function<decltype([](int) noexcept { return 1; })>;
using PlusLambda = refl::Function<decltype(+[](int) noexcept { return 1; })>;
inline constexpr auto capturing = [factor = 2](int value) noexcept { return value * factor; };
using CapturingLambda = refl::Function<decltype(capturing)>;
using BorrowedLambda = refl::Function<std::reference_wrapper<decltype(capturing)>>;
static_assert(BareLambda::isMember && BareLambda::isConst && BareLambda::isNoexcept);
static_assert(!PlusLambda::isMember && PlusLambda::isNoexcept);
static_assert(CapturingLambda::isMember && CapturingLambda::arity == 1);
static_assert(BorrowedLambda::isMember && BorrowedLambda::arity == 1);

using Slot = refl::Function<telemetry::FunctionSlot<int(int) noexcept>>;
using ContextSlot = refl::Function<telemetry::ContextFunctionSlot<int(int) noexcept>>;
using DelegateRefSlot = refl::Function<telemetry::DelegateRefSlot<int(int) noexcept>>;
using DelegateSlot = refl::Function<telemetry::DelegateSlot<int(int) noexcept, 32, 8>>;
static_assert(std::is_same_v<Slot::Result, int> && Slot::arity == 1);
static_assert(std::is_same_v<ContextSlot::Result, int> && ContextSlot::arity == 1);
static_assert(std::is_same_v<DelegateRefSlot::Arguments, std::tuple<int>>);
static_assert(std::is_same_v<DelegateSlot::Arguments, std::tuple<int>>);

using Service = refl::EndpointTraits<refl::EndpointKind::Service,
                                     decltype(&facade_probe::Owner::read)>;
static_assert(std::is_same_v<Service::Request, facade_probe::Request>);
static_assert(std::is_same_v<Service::Response, facade_probe::Response>);
static_assert(Service::kind == refl::EndpointKind::Service);

using Wrapped = refl::EndpointTraits<refl::EndpointKind::Service,
    telemetry::ServiceResult<facade_probe::Response>(
        const facade_probe::Request&) noexcept>;
static_assert(Wrapped::wrapsServiceResult);
static_assert(std::is_same_v<Wrapped::Response, facade_probe::Response>);

using Empty = refl::EndpointTraits<refl::EndpointKind::Service, void() noexcept>;
using WrappedEmpty = refl::EndpointTraits<refl::EndpointKind::Service,
    telemetry::ServiceResult<void>() noexcept>;
using ByValue = refl::EndpointTraits<refl::EndpointKind::Service,
    facade_probe::Response(facade_probe::Request) noexcept>;
static_assert(std::is_void_v<Empty::Request> && std::is_void_v<Empty::Response>);
static_assert(WrappedEmpty::wrapsServiceResult && std::is_void_v<WrappedEmpty::Response>);
static_assert(std::is_same_v<ByValue::Request, facade_probe::Request>);

using CommandStatus = refl::EndpointTraits<refl::EndpointKind::Command,
    telemetry::CommandResult(const facade_probe::Request&) noexcept>;
using FieldStatus = refl::EndpointTraits<refl::EndpointKind::Field,
    telemetry::WriteResult(const facade_probe::Request&) noexcept>;
static_assert(std::is_same_v<CommandStatus::Result, telemetry::CommandResult>
              && std::is_void_v<CommandStatus::Response>);
static_assert(std::is_same_v<FieldStatus::Result, telemetry::WriteResult>
              && std::is_void_v<FieldStatus::Response>);

extern "C" int structured_facade_other() noexcept;

int main()
{
    MeterConfig value{3.0f, 42};
    refl::get<0>(value) = 4.0f;
    return refl::get<0>(value) == 4.0f && structured_facade_other() == 2 ? 0 : 1;
}
