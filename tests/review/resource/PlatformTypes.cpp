// Review probe (resource slice): wire type chosen for platform-dependent C++ types.
// Each compiler reports the values through a deliberately incomplete template.
#include <telemetry/Telemetry.h>
#include <resource/telemetry/BinaryFormat.hpp>
template <int Char, int Long, int WChar, int CharEnum> struct WireTypes;
enum class CharCoded : char { A };
using telemetry::Scalar;
using telemetry_resource::toWireType;
constexpr int code(telemetry::ScalarType t) { return static_cast<int>(toWireType(t)); }
WireTypes<code(Scalar::from(char{}).type()), code(Scalar::from(long{}).type()),
          code(Scalar::from(wchar_t{}).type()), code(telemetry::enumType<CharCoded>())> probe;
