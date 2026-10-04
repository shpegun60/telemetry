/*
 * @file StorageAbiCaller.cpp
 * @brief A real adapter reference must reject a different object budget.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#include <telemetry/model/Adapter.hpp>

int main()
{
	namespace ts = telemetry;
	ts::Workspace workspace{std::span<std::byte>{}};
	ts::ModelView view{ts::TypeRegistryView{}, ts::ServiceIndex{nullptr, 0}, nullptr, 0};
	return static_cast<int>(ts::readFieldEncoded(view, 0u, {}, workspace).dispatch);
}
