/* Public Workbench compiles without Model, reflection or concrete backend. MIT. */
#include <integration/workbench/WorkbenchModel.hpp>
#include <type_traits>
using Workbench = telemetry::integration::workbench::WorkbenchModel<2, 128, 128>;
static_assert(!std::is_copy_constructible_v<Workbench> && !std::is_move_constructible_v<Workbench>);

extern "C" bool workbench_edit(Workbench& model,
                               const telemetry::integration::inspector::NodeHandle& node) noexcept
{
	return model.setScalar(node, std::uint64_t{UINT64_MAX}) == telemetry::runtime::Status::Ok &&
	       bool(model.apply([](const telemetry::integration::workbench::ApplyResult&) noexcept {
	       }));
}

int main()
{
	return sizeof(Workbench) > sizeof(void*) ? 0 : 1;
}
