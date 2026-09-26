// Review probe (catalog-json): the schema type inferred for plain char, long
// and wchar_t getters depends on the target ABI, so the same definition gives a
// different "t" and schemaCrc on the host and on Cortex-M7.
#include "Telemetry.h"
using namespace telemetry;
constexpr auto charType = Scalar::from(char{}).type();
constexpr auto longType = Scalar::from(long{}).type();
constexpr auto wcharType = Scalar::from(wchar_t{}).type();
#if defined(__arm__)
static_assert(charType == ScalarType::U8 && longType == ScalarType::S32 && wcharType == ScalarType::U32,
              "ARM EABI: char unsigned, long 32-bit, wchar_t unsigned 32-bit");
#elif defined(_WIN32)
static_assert(charType == ScalarType::S8 && longType == ScalarType::S32 && wcharType == ScalarType::U16,
              "Windows x64: char signed, long 32-bit, wchar_t unsigned 16-bit");
#else
static_assert(charType == ScalarType::S8 && longType == ScalarType::S64 && wcharType == ScalarType::S32,
              "Linux x86-64: char signed, long 64-bit, wchar_t signed 32-bit");
#endif
int main() { return 0; }
