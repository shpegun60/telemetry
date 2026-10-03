/*
 * @file ArmAdapterLink.cpp
 * @brief Small positive ARM link for the compiled encoded-Service boundary.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */

#include <telemetry/model/Adapter.hpp>

int main()
{
    namespace ts = telemetry;
    ts::Workspace workspace{std::span<std::byte>{}};
    ts::ModelView view{ts::TypeRegistryView{}, ts::ServiceIndex{nullptr, 0}, nullptr, 0};
    const auto result = ts::callServiceEncoded(view, telemetry::PackedId{0},
                                               {}, {}, workspace);
    return result.dispatch == ts::DispatchStatus::NotFound ? 0 : 1;
}
