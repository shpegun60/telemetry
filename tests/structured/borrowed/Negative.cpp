/* One intended diagnostic per rejected borrowed declaration/factory. MIT. */
#include "Fixture.hpp"
#include <utility>
using namespace borrowed_fixture;

struct Other {
	std::uint32_t value;
	bool enabled;
};

struct Proxy {
	operator const Config&() const noexcept
	{
		return first.value;
	}
};

Config& mutableValue() noexcept;
Config&& rvalueValue() noexcept;
const Config&& constRvalueValue() noexcept;
const volatile Config& volatileValue() noexcept;
Config* pointerValue() noexcept;
const Config* constPointerValue() noexcept;
void voidValue() noexcept;
const Config constValue() noexcept;
const Config& throwingValue();
const std::uint32_t& scalarValue() noexcept;
const std::array<std::uint8_t, 2>& arrayValue() noexcept;
ts::BorrowedServiceResult<Config> wrappedBorrowed() noexcept;
const ts::BorrowedServiceResult<Config> constWrappedBorrowed() noexcept;
ts::BorrowedServiceResult<std::uint32_t> scalarWrappedBorrowed() noexcept;
ts::BorrowedServiceResult<std::array<std::uint8_t, 2>> arrayWrappedBorrowed() noexcept;
ts::BorrowedServiceResult<const Config> constPayload() noexcept;
ts::BorrowedServiceResult<volatile Config> volatilePayload() noexcept;
ts::BorrowedServiceResult<Config&> referencePayload() noexcept;
ts::BorrowedServiceResult<void> voidPayload() noexcept;
const ts::BorrowedServiceResult<Config>& wrappedBorrowedReference() noexcept;
const ts::ServiceResult<Config>& wrappedOwnedReference() noexcept;
const ts::BorrowedValue<Config>& wrappedViewReference() noexcept;
const Config& pointerRequest(const Request*) noexcept;
const Config& volatileRequest(const volatile Request&) noexcept;
const Config& scalarRequest(std::uint8_t) noexcept;
const Config& twoRequests(const Request&, int) noexcept;
WR wrongSetter(const Other&) noexcept;
WR mutableSetter(Config&) noexcept;
bool wrongStatus(const Config&) noexcept;

void reject()
{
	Config value{};
	const Config constant{};
	volatile Config volatileObject{};
	Proxy proxy{};
	Config* pointer = &value;
	(void)value;
	(void)constant;
	(void)volatileObject;
	(void)proxy;
	(void)pointer;
#if CASE == 1
	(void)ts::BorrowedValue<Config>::from(Config{});
#elif CASE == 2
	(void)ts::BorrowedValue<Config>::from(std::move(constant));
#elif CASE == 3
	(void)ts::BorrowedValue<Config>::from({Config{}});
#elif CASE == 4
	(void)ts::BorrowedValue<Config>::from(proxy);
#elif CASE == 5
	(void)ts::BorrowedValue<Config>::from(volatileObject);
#elif CASE == 6
	(void)ts::BorrowedValue<Config>::from<const Config&>(Config{});
#elif CASE == 7
	(void)ts::BorrowedValue<Config>::from<const Config&>(value);
#elif CASE == 8
	(void)ts::BorrowedValue<Config>::from(pointer);
#elif CASE == 9
	(void)ts::BorrowedServiceResult<Config>::success(Config{});
#elif CASE == 10
	(void)ts::BorrowedServiceResult<Config>::success(std::move(constant));
#elif CASE == 11
	(void)ts::BorrowedServiceResult<Config>::success({Config{}});
#elif CASE == 12
	(void)ts::BorrowedServiceResult<Config>::success(proxy);
#elif CASE == 13
	(void)ts::BorrowedServiceResult<Config>::success(volatileObject);
#elif CASE == 14
	(void)ts::BorrowedServiceResult<Config>::success<const Config&>(Config{});
#elif CASE == 15
	(void)ts::BorrowedServiceResult<Config>::success<const Config&>(value);
#elif CASE == 16
	(void)ts::BorrowedServiceResult<Config>::success(pointer);
#elif CASE == 17
	(void)ts::field<&wrappedBorrowed>("Wrapper Field");
#elif CASE == 18
	(void)ts::field<&mutableValue>("Mutable");
#elif CASE == 19
	(void)ts::field<&rvalueValue>("Rvalue");
#elif CASE == 20
	(void)ts::field<&constRvalueValue>("Const rvalue");
#elif CASE == 21
	(void)ts::field<&volatileValue>("Volatile");
#elif CASE == 22
	(void)ts::field<&pointerValue>("Pointer");
#elif CASE == 23
	(void)ts::field<&constPointerValue>("Const pointer");
#elif CASE == 24
	(void)ts::field<&voidValue>("Void");
#elif CASE == 25
	(void)ts::field<&constValue>("Const value");
#elif CASE == 26
	(void)ts::service<&mutableValue>("Mutable");
#elif CASE == 27
	(void)ts::service<&rvalueValue>("Rvalue");
#elif CASE == 28
	(void)ts::service<&constRvalueValue>("Const rvalue");
#elif CASE == 29
	(void)ts::service<&volatileValue>("Volatile");
#elif CASE == 30
	(void)ts::service<&pointerValue>("Pointer");
#elif CASE == 31
	(void)ts::service<&constPointerValue>("Const pointer");
#elif CASE == 32
	(void)ts::service<&scalarValue>("Scalar");
#elif CASE == 33
	(void)ts::service<&arrayValue>("Array");
#elif CASE == 34
	(void)ts::service<&wrappedBorrowedReference>("Wrapped reference");
#elif CASE == 35
	(void)ts::service<&wrappedOwnedReference>("Wrapped reference");
#elif CASE == 36
	(void)ts::field<&wrappedViewReference>("Wrapped reference");
#elif CASE == 37
	(void)ts::field<&throwingValue>("Throwing");
#elif CASE == 38
	(void)ts::service<&throwingValue>("Throwing");
#elif CASE == 39
	(void)ts::field<&getFirst, &wrongSetter>("Wrong setter");
#elif CASE == 40
	(void)ts::field<&getFirst, &mutableSetter>("Mutable setter");
#elif CASE == 41
	(void)ts::field<&getFirst, &wrongStatus>("Wrong status");
#elif CASE == 42
	(void)ts::service<&pointerRequest>("Pointer request");
#elif CASE == 43
	(void)ts::service<&volatileRequest>("Volatile request");
#elif CASE == 44
	(void)ts::service<&scalarRequest>("Scalar request");
#elif CASE == 45
	(void)ts::service<&twoRequests>("Two requests");
#elif CASE == 46
	(void)ts::field<&Device::get>("Temporary owner", Device{});
#elif CASE == 47
	(void)ts::service<&Device::call>("Temporary owner", Device{});
#elif CASE == 48
	(void)sizeof(ts::BorrowedValue<const Config>);
#elif CASE == 49
	(void)sizeof(ts::BorrowedValue<Config&>);
#elif CASE == 50
	(void)sizeof(ts::BorrowedValue<Config*>);
#elif CASE == 51
	(void)sizeof(ts::BorrowedServiceResult<const Config>);
#elif CASE == 52
	(void)ts::service<&constWrappedBorrowed>("Const wrapper");
#elif CASE == 53
	(void)ts::service<&scalarWrappedBorrowed>("Scalar wrapper");
#elif CASE == 54
	(void)ts::service<&arrayWrappedBorrowed>("Array wrapper");
#elif CASE == 55
	(void)sizeof(ts::BorrowedServiceResult<void>);
#elif CASE == 56
	(void)sizeof(ts::BorrowedServiceResult<Config*>);
#elif CASE == 57
	(void)sizeof(ts::BorrowedServiceResult<Config&>);
#elif CASE == 58
	(void)ts::BorrowedValue<Config>::from({});
#elif CASE == 59
	(void)ts::BorrowedServiceResult<Config>::success({});
#elif CASE == 60
	constexpr auto definition = ts::service<&constPayload>("Const payload");
	(void)definition;
#elif CASE == 61
	constexpr auto definition = ts::service<&volatilePayload>("Volatile payload");
	(void)definition;
#elif CASE == 62
	constexpr auto definition = ts::service<&referencePayload>("Reference payload");
	(void)definition;
#elif CASE == 63
	constexpr auto definition = ts::service<&voidPayload>("Void payload");
	(void)definition;
#else
#error CASE must select one intended rejection
#endif
}
