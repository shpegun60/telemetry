/* Public Workbench does not expose Commands/Services or its private client. MIT. */
#include <integration/workbench/WorkbenchModel.hpp>
using Workbench = telemetry::integration::workbench::WorkbenchModel<2, 128, 128>;

void probe(Workbench& model)
{
	(void)model;
#ifdef WORKBENCH_COMMAND
	model.command();
#endif
#ifdef WORKBENCH_SERVICE
	model.service();
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
}

int main()
{
	return 0;
}
