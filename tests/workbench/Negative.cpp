/* Public Workbench excludes raw requests, private clients and invalid callbacks. MIT. */
#include <integration/workbench/WorkbenchModel.hpp>
using Workbench = telemetry::integration::workbench::WorkbenchModel<2, 128, 128>;

void probe(Workbench& model)
{
	(void)model;
#ifdef WORKBENCH_COMMAND
	// Runtime calls remain behind the bound editor, never a raw-payload facade.
	(void)model.command(telemetry::integration::dynamic::EndpointBinding{},
	                    telemetry::client::Input{}, [](const auto&) noexcept {
	                    });
#endif
#ifdef WORKBENCH_SERVICE
	(void)model.service(telemetry::integration::dynamic::EndpointBinding{},
	                    telemetry::client::Input{}, [](const auto&) noexcept {
	                    });
#endif
#ifdef WORKBENCH_CLIENT
	(void)model.client();
#endif
#ifdef WORKBENCH_CORE
	(void)model.core();
#endif
#ifdef WORKBENCH_RAW
	(void)model.submitEncoded();
#endif
#ifdef WORKBENCH_INSPECTOR
	(void)model.model_;
#endif
#ifdef WORKBENCH_NULL_CALLBACK
	(void)model.apply(nullptr);
#endif
#ifdef WORKBENCH_NULL_COMMAND_CALLBACK
	(void)model.execute(nullptr);
#endif
#ifdef WORKBENCH_NULL_SERVICE_CALLBACK
	(void)model.callService(nullptr);
#endif
}

int main()
{
	return 0;
}
