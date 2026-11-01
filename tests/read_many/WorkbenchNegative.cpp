/* Optional Workbench bridge excludes raw clients and invalid callback ownership. MIT. */
#include <integration/read_many/Workbench.hpp>
using Workbench = telemetry::integration::workbench::WorkbenchModel<2, 130, 64>;
namespace rm = telemetry::integration::read_many;

void probe(Workbench& owner, const rm::ReadManyBinding& binding)
{
	(void)owner;
	(void)binding;
#ifdef READ_MANY_WORKBENCH_CORE
	(void)owner.core();
#endif
#ifdef READ_MANY_WORKBENCH_CLIENT
	(void)owner.client();
#endif
#ifdef READ_MANY_WORKBENCH_PRIVATE
	(void)owner.readOnlyClient();
#endif
#ifdef READ_MANY_WORKBENCH_CALLBACK_TOO_SMALL
	(void)rm::readMany(owner, binding, [](const auto&) noexcept {
	});
#endif
#ifdef READ_MANY_WORKBENCH_THROWING_CALLBACK
	(void)rm::readFieldValue(owner, {}, [](const auto&) {
	});
#endif
#ifdef READ_MANY_WORKBENCH_NULL_CALLBACK
	(void)rm::readMany(owner, binding, nullptr);
#endif
}
