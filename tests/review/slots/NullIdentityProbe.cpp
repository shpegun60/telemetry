// Review probe (slots): under GCC -fno-delete-null-pointer-checks, an address
// comparison of an NTTP function is non-constant, but tiny_delegate's identity
// comparison (detail::non_null_target_v) stays a constant expression.
#include "../../../lib/delegate/tiny_delegate.hpp"
struct Binding { static int read(int& v) noexcept { return v; } };
template <auto Adapter> constexpr bool identityCheck() { return tiny::detail::non_null_target_v<Adapter>; }
static_assert(identityCheck<&Binding::read>(), "identity comparison is constant");
#if defined(REVIEW_ADDRESS_COMPARE)
template <auto Adapter> constexpr bool addressCheck() { return Adapter != nullptr; }
static_assert(addressCheck<&Binding::read>(), "address comparison");
#endif
int main() {}
