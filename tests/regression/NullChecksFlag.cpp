// Regression promoted from tests/review/slots/NullChecksFlag.cpp.
// Authors: Ruslan Kovtun (shpegun60), codexAi. License: MIT.
// Review repro (slots): with GCC, -fno-delete-null-pointer-checks (also implied
// by -fsanitize=null, nonnull-attribute and returns-nonnull-attribute, i.e. by
// -fsanitize=undefined) makes `&function != nullptr` non-constant, so every
// static_assert(Adapter != nullptr) and every constexpr Getter/Setter built from
// a function pointer fails. A single slot-backed field is enough to break the build.
//   g++ -std=c++17 -Ilib/telemetry -Ilib/delegate -fno-delete-null-pointer-checks -fsyntax-only <this>
// The bundled tiny_delegate avoids the same trap with detail::non_null_target_v.
#include "Telemetry.h"
using namespace telemetry;

FunctionSlot<float() noexcept> reading;
constexpr FieldTable rows{field("Value", "V", reading)};

#if defined(REVIEW_SLOT_CONSTEXPR_BIND)
float source() noexcept { return 1.f; }
constexpr bool boundInConstantEvaluation() noexcept
{
    FunctionSlot<float() noexcept> local;
    local.bind(&source);
    return local.available(); // function_ != nullptr on a function address
}
static_assert(boundInConstantEvaluation());
#endif

int main() { return rows.read<0>() ? 1 : 0; }
