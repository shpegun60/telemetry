#include "mainwindow.h"

#include <QApplication>
#include <QTimer>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    MainWindow w;
    w.show();
    if (a.arguments().contains("--smoke-test")) {
        demo::DemoCatalog fixture;
        if (fixture.commands().call(0) != telemetry::CommandResult::Executed
            || fixture.commands().call(1, 275.0f, 2) != telemetry::CommandResult::Executed
            || fixture.index().read<float>(telemetry::makeId(0, 4)) != 275.0f
            || fixture.commands().call(1, 1001.0f, 2) != telemetry::CommandResult::InvalidValue)
            return 2;
        QTimer::singleShot(1200, &a, &QCoreApplication::quit);
    }
    return QApplication::exec();
}
