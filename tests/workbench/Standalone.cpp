/* Public Workbench compiles without Model, reflection or concrete backend. MIT. */
#include <integration/workbench/WorkbenchModel.hpp>
#include <type_traits>
using Workbench = telemetry::integration::workbench::WorkbenchModel<2, 128, 128>;
static_assert(!std::is_copy_constructible_v<Workbench> && !std::is_move_constructible_v<Workbench>);
using Store = telemetry::integration::inspector::SnapshotStore;
static_assert(std::is_same_v<decltype(&Store::select),
                             telemetry::integration::inspector::ModelStatus (Store::*)(
                                 std::uint32_t, telemetry::TypeId) noexcept>);

static void applyCompletion(const telemetry::integration::workbench::ApplyResult&) noexcept
{}

static void commandCompletion(const telemetry::integration::inspector::OperationResult&) noexcept
{}

static void serviceCompletion(const telemetry::integration::workbench::ServiceResult&) noexcept
{}

extern "C" bool workbench_edit(Workbench& model,
                               const telemetry::integration::inspector::NodeHandle& node) noexcept
{
	return model.setScalar(node, std::uint64_t{UINT64_MAX}) == telemetry::runtime::Status::Ok &&
	       bool(model.apply(applyCompletion));
}

extern "C" bool workbench_command(Workbench& model, std::uint32_t id) noexcept
{
	const auto bound = model.bindCommand(id);
	return bound && model.selectCommand(bound.endpoint) == telemetry::client::Status::Ok &&
	       bool(model.execute(commandCompletion));
}

extern "C" bool workbench_service(Workbench& model, std::uint32_t id) noexcept
{
	const auto bound = model.bindService(id);
	return bound && model.selectService(bound.endpoint) == telemetry::client::Status::Ok &&
	       bool(model.callService(serviceCompletion));
}

int main()
{
	return sizeof(Workbench) > sizeof(void*) ? 0 : 1;
}
