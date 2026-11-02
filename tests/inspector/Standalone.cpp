/*
 * @file Standalone.cpp
 * @brief Inspector public API requires no model, reflection or concrete transport.
 * @author Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT
 */
#include <integration/inspector/InspectorModel.hpp>
#include <type_traits>

using Inspector = telemetry::integration::inspector::InspectorModel<2, 64, 64>;
static_assert(!std::is_copy_constructible_v<Inspector>);
static_assert(!std::is_move_constructible_v<Inspector>);

static void inspectorCompletion(const telemetry::integration::inspector::ReadResult&) noexcept
{}

extern "C" bool
inspector_read(Inspector& inspector,
               const telemetry::integration::dynamic::EndpointBinding& field) noexcept
{
	return bool(inspector.readField(field, inspectorCompletion));
}

extern "C" bool inspector_handoff(Inspector& inspector, std::uint32_t session,
                                  std::uint32_t request) noexcept
{
	inspector.process(0);
	return inspector.prepareHandoff(session, request);
}

extern "C" bool inspector_decimal(Inspector& inspector,
                                  const telemetry::integration::inspector::NodeHandle& node,
                                  std::span<char> output) noexcept
{
	return bool(inspector.decimal(node, output));
}

extern "C" bool inspector_child(Inspector& inspector,
                                const telemetry::integration::inspector::NodeHandle& node,
                                std::uint32_t index) noexcept
{
	const auto child = inspector.child(node, index);
	return child && bool(inspector.info(child.value));
}

int main()
{
	return sizeof(Inspector) > sizeof(void*) ? 0 : 1;
}
