// GNU/ELF weak targets may be absent after linking. Every public invocation
// must report absence instead of calling address zero (MIT).
// Authors: Ruslan Kovtun (shpegun60), codexAi.
#include "Telemetry.h"

#if defined(__ELF__) && (defined(__GNUC__) || defined(__clang__))

using namespace telemetry;

extern "C" float missingRead() noexcept __attribute__((weak));
extern "C" WriteResult missingWrite(float) noexcept __attribute__((weak));
extern "C" CommandResult missingCommand() noexcept __attribute__((weak));
Scalar missingScalarRead() noexcept __attribute__((weak));
extern "C" WriteResult missingScalarWrite(const Scalar&) noexcept __attribute__((weak));
Scalar missingContextRead(int&) noexcept __attribute__((weak));
extern "C" WriteResult missingContextWrite(int&, const Scalar&) noexcept __attribute__((weak));
extern "C" float missingSlotContext(void*) noexcept __attribute__((weak));

enum class Mode : unsigned { Off, On };
extern "C" Mode missingEnumRead() noexcept __attribute__((weak));

struct Device {
    float read() const noexcept __attribute__((weak));
    WriteResult write(float) noexcept __attribute__((weak));
    CommandResult run() noexcept __attribute__((weak));
};
Device device;

extern "C" __attribute__((weak)) float defaultRead() noexcept { return 7.f; }

int context = 0;
constexpr FieldTable fields{
    field<&missingRead, &missingWrite>("Missing", ""),
    field<&missingEnumRead>("Enum", ""),
    field<&defaultRead>("Default", ""),
};
constexpr FieldTable parameterFields{
    field("Missing parameter pair", "", &missingRead, &missingWrite),
};
constexpr CommandTable commands{command<&missingCommand>("Missing command")};
constexpr FieldTable memberFields{
    field<&Device::read, &Device::write>("Missing member", "", device),
};
constexpr CommandTable memberCommands{
    command<&Device::run>("Missing member command", device),
};
constexpr Field scalarField{"Scalar", "", ScalarType::F32,
                            Getter::bind<&missingScalarRead>(),
                            Setter::bind<&missingScalarWrite>()};
constexpr Field scalarPointerField{"Scalar pointer", "", ScalarType::F32,
                                   &missingScalarRead, &missingScalarWrite};
constexpr Getter contextGetter = Getter::bindContext<&missingContextRead>(context);
constexpr Setter contextSetter = Setter::bindContext<&missingContextWrite>(context);

int main()
{
    bool ok = !fields.read<0>() && !fields.read<0, double>()
        && fields[0].read().type() == ScalarType::Null
        && fields.write<0>(1.f) == WriteResult::Unavailable
        && !fields.read<1>() && fields[1].read().type() == ScalarType::Null
        && fields.read<2>() == 7.f
        && !parameterFields.read<0>()
        && parameterFields.write<0>(2.f) == WriteResult::Unavailable
        && commands.call<0>() == CommandResult::Unavailable
        && commands[0].execute(nullptr, 0) == CommandResult::Unavailable
        && !memberFields.read<0>()
        && memberFields.write<0>(1.f) == WriteResult::Unavailable
        && memberCommands.call<0>() == CommandResult::Unavailable
        && scalarField.read().type() == ScalarType::Null
        && scalarField.write(3.f) == WriteResult::Unavailable
        && scalarPointerField.read().type() == ScalarType::Null
        && scalarPointerField.write(3.f) == WriteResult::Unavailable
        && contextGetter().type() == ScalarType::Null
        && contextSetter(Scalar::from(4.f)) == WriteResult::Unavailable;

    FunctionSlot<float() noexcept> functionSlot;
    functionSlot.bind(&missingRead);
    ok = ok && !functionSlot.available();
    ContextFunctionSlot<float() noexcept> contextSlot;
    contextSlot.bind(&missingSlotContext, nullptr);
    ok = ok && !contextSlot.available();

    DelegateRefSlot<float() noexcept> slot;
    slot.bind<&missingRead>();
    ok = ok && !slot.available();
    slot.bind<&defaultRead>();
    ok = ok && slot.available() && slot.get().invoke() == 7.f;
    slot.bind<&Device::read>(device);
    ok = ok && !slot.available();
    return ok ? 0 : 1;
}

#else
int main() { return 0; }
#endif
