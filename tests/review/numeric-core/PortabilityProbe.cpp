// Review probe (numeric-core): which Scalar alternative do platform-dependent
// C++ integer types select, and which exact accessors exist on each target?
#include "core/TelemetryScalar.h"
using telemetry::Scalar; using telemetry::ScalarType;
#if defined(__arm__)
static_assert(Scalar::from(char{}).type() == ScalarType::U8, "ARM: char is unsigned");
static_assert(Scalar::from(long{}).type() == ScalarType::S32, "ARM: long is 32-bit");
static_assert(Scalar::from(std::size_t{}).type() == ScalarType::U32, "ARM: size_t is 32-bit");
#else
static_assert(Scalar::from(char{}).type() == ScalarType::S8, "x86: char is signed");
static_assert(Scalar::from(std::size_t{}).type() == ScalarType::U64, "x86-64: size_t is 64-bit");
#endif
#ifdef PROBE_GET_INT
int probe(const Scalar& value) { return value.get<int>(); }   // exact accessor spelled with int
#endif
#ifdef PROBE_GET_LONG_LONG
long long probe(const Scalar& value) { return value.get<long long>(); }
#endif
int main() { return 0; }
