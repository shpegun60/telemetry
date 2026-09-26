/*
 * @file FacadeNegative.cpp
 * @brief One intentional compile failure per unsupported signature.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */

#include "../reflection/ProbeTypes.hpp"

#include <telemetry_structured/reflection/Reflection.hpp>

namespace refl = telemetry::structured::reflection;
using Kind = refl::EndpointKind;

struct Request { int value; };

struct Methods {
    int observed() volatile noexcept;
    int temporary() && noexcept;
};

struct Overloaded {
    int operator()(int) const noexcept;
    int operator()(float) const noexcept;
};

#if CASE == 1
using Invalid = refl::EndpointTraits<Kind::Service, int() /* throwing */>;
#elif CASE == 2
using Invalid = refl::EndpointTraits<Kind::Service, decltype(&Methods::observed)>;
#elif CASE == 3
using Invalid = refl::EndpointTraits<Kind::Service, decltype(&Methods::temporary)>;
#elif CASE == 4
using Invalid = refl::EndpointTraits<Kind::Service, int(int, ...) noexcept>;
#elif CASE == 5
using Invalid = refl::EndpointTraits<Kind::Service, int(Request&) noexcept>;
#elif CASE == 6
using Invalid = refl::EndpointTraits<Kind::Service, const Request&() noexcept>;
#elif CASE == 7
using Invalid = refl::EndpointTraits<Kind::Service, Request*() noexcept>;
#elif CASE == 8
using Invalid = refl::EndpointTraits<Kind::Service, int(Request*) noexcept>;
#elif CASE == 9
using Invalid = refl::EndpointTraits<Kind::Service, int(int, int) noexcept>;
#elif CASE == 10
using Invalid = refl::Function<Overloaded>;
#elif CASE == 11
using Invalid = refl::Function<decltype([](auto) noexcept { return 1; })>;
#elif CASE == 12
static_assert(refl::memberName<0, telemetry_structured_probe::NonAsciiName>().empty());
#elif CASE == 13
int readTemporary()
{
    return static_cast<int>(refl::get<0>(Request{1}));
}
#elif CASE == 14
using Invalid = refl::EndpointTraits<Kind::Service, int(const volatile Request&) noexcept>;
#else
#error "CASE must select a deliberate rejection"
#endif

#if CASE != 12 && CASE != 13
static_assert(sizeof(Invalid) > 0);
#endif
