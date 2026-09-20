// C++20 structural NTTP adapters must validate their actual const-lvalue call,
// including its exception specification, rather than a convertible pointer or
// an rvalue-qualified overload that the generated invoker never uses.
#include "field/TelemetryGetter.h"
#include "field/TelemetrySetter.h"

using namespace telemetry;
struct Owner {} owner;

#if TELEMETRY_ADAPTER_FAIL_CASE == 1
WriteResult pointerTarget(const Scalar&) noexcept { return WriteResult::Applied; }
struct Adapter {
    constexpr operator Setter::Function() const noexcept { return &pointerTarget; }
    WriteResult operator()(const Scalar&) const { return WriteResult::Applied; }
};
constexpr auto rejected = Setter::bind<Adapter{}>();
#elif TELEMETRY_ADAPTER_FAIL_CASE == 2
struct Adapter {
    float operator()(Owner&) const && noexcept { return 1.0f; }
    float operator()(Owner&) const & { return 2.0f; }
};
constexpr auto rejected = Getter::bindContext<Adapter{}>(owner);
#elif TELEMETRY_ADAPTER_FAIL_CASE == 3
struct Adapter {
    WriteResult operator()(Owner&, const Scalar&) const && noexcept { return WriteResult::Applied; }
    WriteResult operator()(Owner&, const Scalar&) const & { return WriteResult::Applied; }
};
constexpr auto rejected = Setter::bindContext<Adapter{}>(owner);
#else
#error "Select TELEMETRY_ADAPTER_FAIL_CASE from 1 through 3"
#endif

int main() {}
