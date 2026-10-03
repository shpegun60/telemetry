// Missing and defined ELF targets remain distinct for shared slots (MIT).
#include <telemetry/slot/TelemetryFunctionSlot.h>
#include <telemetry/slot/TelemetryContextFunctionSlot.h>
#include <telemetry/slot/TelemetryDelegateRefSlot.h>
#include "SharedSupport.hpp"
using namespace telemetry;
#if defined(__ELF__) && (defined(__GNUC__) || defined(__clang__))
extern "C" float missingRead() noexcept __attribute__((weak));
extern "C" float missingContext(void*) noexcept __attribute__((weak));
extern "C" __attribute__((weak)) float defaultRead() noexcept { return 7; }
struct Owner { float read() const noexcept __attribute__((weak)); };
#endif
int main() {
#if defined(__ELF__) && (defined(__GNUC__) || defined(__clang__))
    FunctionSlot<float() noexcept> function; function.bind(&missingRead); CHECK(!function.available());
    ContextFunctionSlot<float() noexcept> context; context.bind(&missingContext, nullptr); CHECK(!context.available());
    DelegateRefSlot<float() noexcept> ref; ref.bind<&missingRead>(); CHECK(!ref.available());
    ref.bind<&defaultRead>(); CHECK(ref.available() && ref.invoke() == 7);
    Owner owner; ref.bind<&Owner::read>(owner); CHECK(!ref.available());
#else
    std::printf("SKIP ELF weak target execution\n");
#endif
    reportChecks();
}
