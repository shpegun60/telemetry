// Critic trial: minimal Cortex-M7 main that keeps the generated scaling catalog reachable.
#include <cstdint>

namespace telemetry {
class Scalar;
}
extern "C" unsigned scaleRead(unsigned id) noexcept;
extern "C" int scaleWrite(unsigned id, float v) noexcept;
extern "C" int scaleExec(unsigned id, const telemetry::Scalar* a, unsigned n) noexcept;
#ifdef CRITIC_JSON
extern "C" unsigned scaleJson(char* out, unsigned size) noexcept;
char scaleBuffer[256];
#endif

volatile unsigned scaleId;
volatile float scaleValue;
volatile unsigned scaleSink;

int main()
{
    for (;;) {
        const unsigned id = scaleId;
        scaleSink = scaleRead(id) + static_cast<unsigned>(scaleWrite(id, scaleValue)) +
                    static_cast<unsigned>(scaleExec(id, nullptr, 0));
#ifdef CRITIC_JSON
        scaleSink = scaleSink + scaleJson(scaleBuffer, sizeof(scaleBuffer));
#endif
        if (id == 0xffffffffu) {
            break;
        }
    }
    return 0;
}
