#include "mainwindow.h"

#include <QApplication>
#include <QTimer>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    MainWindow w;
    w.show();
    if (a.arguments().contains("--smoke-test")) {
        QTimer::singleShot(1200, &a, &QCoreApplication::quit);
    }
    return QApplication::exec();
}
