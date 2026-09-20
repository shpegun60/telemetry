#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include "demo/DemoCatalog.h"

class QPlainTextEdit;
class QTableWidget;

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private:
    void refreshValues();

    Ui::MainWindow *ui;
    QTableWidget* fields_ = nullptr;
    QPlainTextEdit* values_ = nullptr;
};
#endif // MAINWINDOW_H
