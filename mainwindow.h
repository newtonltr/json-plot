#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

#include <memory>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class DataRepository;
class SocketManager;
class WatchWidget;
class PlotWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private:
    Ui::MainWindow *ui;
    std::unique_ptr<DataRepository> m_repository;
    std::unique_ptr<SocketManager> m_socketManager;
    WatchWidget *m_watchWidget = nullptr;
    PlotWidget *m_plotWidget = nullptr;
};
#endif // MAINWINDOW_H
