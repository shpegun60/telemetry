// Constant-evaluation pointer presence under null-check compiler modes (MIT).
// Exercises known function, method and slot construction across pointer-presence configurations.
// Compile-time controls distinguish valid targets from null-target refusals without invoking absent callbacks.

#include <telemetry/Telemetry.hpp>
using namespace telemetry;

// Known owner methods used as constexpr target-presence controls.
// Public methods:
// - read(): Return fixed value.
// - write(): Return write status.
// - run(): Return command status.
struct Owner {
	float read() const noexcept
	{
		return 1;
	}

	WriteResult write(float) noexcept
	{
		return WriteResult::Applied;
	}

	CommandResult run() noexcept
	{
		return CommandResult::Executed;
	}
};
enum class Mode : std::uint8_t {
	Off,
	On
};

float readFree() noexcept
{
	return 1;
}

WriteResult writeFree(float) noexcept
{
	return WriteResult::Applied;
}

CommandResult runFree() noexcept
{
	return CommandResult::Executed;
}

Mode readMode() noexcept
{
	return Mode::On;
}

Owner owner;
OwnerSlot<Owner> ownerSlot;
FunctionSlot<float() noexcept> functionSlot;
DelegateSlot<float() noexcept> ownedSlot;
DelegateRefSlot<float() noexcept> borrowedSlot;
ContextFunctionSlot<float() noexcept> contextSlot;
#if CASE == 1
constexpr FieldTable probe{field("value", functionSlot)};
#elif CASE == 2
constexpr FieldTable probe{field("value", ownedSlot)};
#elif CASE == 3
constexpr FieldTable probe{field("value", borrowedSlot)};
#elif CASE == 4
constexpr FieldTable probe{field("value", contextSlot)};
#elif CASE == 5
constexpr FieldTable probe{field<&Owner::read, &Owner::write>("value", ownerSlot)};
#elif CASE == 6
constexpr FieldTable probe{field<&readFree, &writeFree>("value")};
#elif CASE == 8
constexpr FieldTable probe{field("value", &readFree, &writeFree)};
#elif CASE == 10
constexpr FieldTable probe{field<&Owner::read, &Owner::write>("value", owner)};
#elif CASE == 11
constexpr CommandTable probe{command<&runFree>("run"), command<&Owner::run>("member", ownerSlot)};
#elif CASE == 12
constexpr FieldTable probe{field<&readMode>("mode")};
#elif CASE == 13
constexpr bool bind() noexcept
{
	FunctionSlot<float() noexcept> local;
	local.bind(&readFree);
	return local.available();
}

constexpr bool probe = bind();
static_assert(probe);
#elif CASE == 20
auto probe = field<static_cast<float (*)() noexcept>(nullptr)>("value");
#elif CASE == 23
auto probe = field<static_cast<float (Owner::*)() const noexcept>(nullptr)>("value", owner);
#elif CASE == 25
constexpr CommandTable probe{command<static_cast<CommandResult (*)() noexcept>(nullptr)>("run")};
#elif CASE == 26
auto probe = field<&readFree, static_cast<WriteResult (*)(float) noexcept>(nullptr)>("value");
#else
#error "Select a maintained null-presence case"
#endif
int main()
{
	(void)&probe;
}
