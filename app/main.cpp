#include "mainwindow.h"

#include <QApplication>
#include <QTimer>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    MainWindow w;
    w.show();
    if (a.arguments().contains("--smoke-test")) {
        if (demo::integerFields.read<0>() != UINT8_MAX
            || demo::fields.read<telemetry::makeId(2, 3)>() != UINT64_MAX
            || demo::meterCommands.call<1>(260.0f, demo::Mode::Auto) != telemetry::CommandResult::Executed
            || demo::commands.call<telemetry::makeId(0, 1)>(270.0f, demo::Mode::Auto) != telemetry::CommandResult::Executed
            || demo::fields.write<telemetry::makeId(0, 4)>(280) != telemetry::WriteResult::Applied
            || demo::meterFields.read<4>() != 280.0f)
            return 3;
        if (demo::commandIndex.call(0) != telemetry::CommandResult::Executed
            || demo::commandIndex.call(1, 275.0f, 2) != telemetry::CommandResult::Executed
            || demo::fieldIndex.read<float>(telemetry::makeId(0, 4)) != 275.0f
            || demo::commandIndex.call(1, 1001.0f, 2) != telemetry::CommandResult::InvalidValue)
            return 2;
        QTimer::singleShot(1200, &a, &QCoreApplication::quit);
    }
    return QApplication::exec();
}
