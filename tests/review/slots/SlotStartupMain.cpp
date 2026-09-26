// Review link probe (slots): minimal newlib-nano program around SlotStartupArm.cpp.
// Link with --specs=nano.specs --specs=nosys.specs and inspect the ELF symbols.
struct StartupMeter { float value = 3.f; float read() const noexcept; };
StartupMeter startupMeter;
float StartupMeter::read() const noexcept { return value; }
extern "C" float probe_read() noexcept;
extern "C" unsigned probe_slot_size() noexcept;
#if defined(PROBE_OWNED)
extern "C" void probe_bind() noexcept;
#endif
int main()
{
#if defined(PROBE_OWNED)
    probe_bind();
#endif
    return static_cast<int>(probe_read()) + static_cast<int>(probe_slot_size());
}
