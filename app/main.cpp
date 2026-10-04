/* Qt playground and its application-level migration smoke.
 * Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT. */
#include "mainwindow.h"
#include "resources/DeviceResources.hpp"

#include <QApplication>
#include <QTableWidget>
#include <QTimer>

#include <array>
#include <limits>

namespace {
bool smokeTest(MainWindow& window)
{
    using telemetry::CommandResult;
    using telemetry::WriteResult;
    constexpr auto limitId = telemetry::makeId<0, 4>();
    constexpr auto modeId = telemetry::makeId<0, 5>();
    constexpr auto configureId = telemetry::makeId<0, 1>();

    if (demo::integerFields.read<0>() != std::numeric_limits<std::uint8_t>::max()
        || demo::fields.read<telemetry::makeId<2, 3>()>() != std::numeric_limits<std::uint64_t>::max()
        || demo::fields.read<telemetry::makeId<2, 7>()>() != std::numeric_limits<std::int64_t>::lowest())
        return false;

    if (demo::meterCommands.call<demo::MeterCommand::Configure>(demo::ConfigureRequest{260.0f, demo::Mode::Auto})
            != CommandResult::Executed
        || demo::commands.call<configureId>(demo::ConfigureRequest{270.0f, demo::Mode::Manual})
            != CommandResult::Executed
        || demo::fields.write<limitId>(280.0f) != WriteResult::Applied
        || demo::meterFields.readAs<double, demo::MeterField::VoltageLimit>() != 280.0
        || demo::meterFields.writeAs<demo::MeterField::VoltageLimit>(285) != WriteResult::Applied
        || demo::fields.writeAs(limitId, 290.0) != WriteResult::Applied
        || demo::fields.readAs<float>(limitId) != 290.0f)
        return false;

    // Runtime callers decode the same ordinary request used by native calls.
    std::array<std::byte, telemetry::wireSize<demo::ConfigureRequest>> input{};
    telemetry::Workspace workspace{std::span<std::byte>{}};
    const auto execute = [&](demo::ConfigureRequest request) {
        if (telemetry::encode(request, input) != telemetry::CodecStatus::Ok)
            return telemetry::EncodedCommandResult{telemetry::DispatchStatus::InternalError};
        return demo::commandIndex.executeEncoded(configureId, input, workspace);
    };
    const auto encoded = execute({300.0f, demo::Mode::Auto});
    if (encoded.dispatch != telemetry::DispatchStatus::Ok
        || encoded.endpointStatus != CommandResult::Executed
        || demo::meter.threshold != 300.0f || demo::meter.mode != demo::Mode::Auto)
        return false;

    // Validation belongs to Meter, and rejects the entire request before a
    // state update. Struct encoding permits representable unnamed enum codes.
    for (float value : {0.5f, 1001.0f, std::numeric_limits<float>::infinity(),
                        std::numeric_limits<float>::quiet_NaN()}) {
        const auto rejected = execute({value, demo::Mode::Manual});
        if (rejected.dispatch != telemetry::DispatchStatus::Ok
            || rejected.endpointStatus != CommandResult::InvalidValue
            || demo::meter.threshold != 300.0f || demo::meter.mode != demo::Mode::Auto
            || demo::fields.writeAs(limitId, value) != WriteResult::InvalidValue
            || demo::meter.threshold != 300.0f)
            return false;
    }
    const auto unknownMode = static_cast<demo::Mode>(3);
    if (execute({310.0f, unknownMode}).endpointStatus != CommandResult::InvalidValue
        || demo::fields.write<modeId>(unknownMode) != WriteResult::InvalidValue
        || demo::meter.threshold != 300.0f || demo::meter.mode != demo::Mode::Auto
        || demo::meter.setThreshold(demo::minimumVoltageLimit) != WriteResult::Applied
        || demo::meter.setThreshold(demo::maximumVoltageLimit) != WriteResult::Applied
        || demo::fields.readAs<float>(telemetry::makeId<3, 0>()).has_value()
        || demo::fields.writeAs(telemetry::makeId<3, 0>(), 10.0f) != WriteResult::NotFound
        || demo::meterCommands.call<demo::MeterCommand::Reset>() != CommandResult::Executed)
        return false;

    // The visible text must preserve the exact integer values, not decimal
    // strings produced through double or a JSON number.
    const auto* table = window.findChild<QTableWidget*>("telemetryFields");
    if (table == nullptr || table->rowCount() != 16
        || table->item(11, 4)->text() != "18446744073709551615"
        || table->item(15, 4)->text() != "-9223372036854775808")
        return false;

    namespace files = device::resources;
    if (files::fileCount() != 2
        || files::path(files::fileIds::Descriptor) != "/telemetry/descriptor.bin"
        || files::path(files::fileIds::Values) != "/telemetry/values.bin"
        || files::stat(2).status != resource::Status::InvalidFile)
        return false;
    std::array<std::byte, 64> prefix{};
    for (const auto index : {files::fileIds::Descriptor, files::fileIds::Values}) {
        const auto stat = files::stat(index);
        const auto result = files::read(index, 0, prefix);
        const char* magic = index == files::fileIds::Descriptor ? "TDS3" : "TVL3";
        if (stat.status != resource::Status::Ok || stat.size < prefix.size()
            || result.status != resource::Status::Ok || result.written < 4
            || !resource::has(stat.flags, resource::FileFlag::Readable)
            || resource::has(stat.flags, resource::FileFlag::Writable))
            return false;
        for (std::size_t i = 0; i < 4; ++i)
            if (prefix[i] != static_cast<std::byte>(magic[i])) return false;
    }
    return true;
}
} // namespace

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    MainWindow window;
    window.show();
    if (application.arguments().contains("--smoke-test")) {
        if (!smokeTest(window)) return 2;
        QTimer::singleShot(1200, &application, &QCoreApplication::quit);
    }
    return QApplication::exec();
}
