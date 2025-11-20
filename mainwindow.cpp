#include "mainwindow.h"
#include "./ui_mainwindow.h"

#include "datarepository.h"
#include "debug_widget.h"
#include "plotwidget.h"
#include "socket_manger.h"
#include "watchwidget.h"

#include <QByteArray>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QWidget>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_repository(std::make_unique<DataRepository>(this))
    , m_socketManager(std::make_unique<SocketManager>(this))
{
    ui->setupUi(this);
    this->setWindowTitle("json plot v1.0");

    // Each tab shares the same repository/socket manager but exposes different tooling.
    auto *tabs = new QTabWidget(this);
    m_watchWidget = new WatchWidget(m_repository.get(), m_socketManager.get(), tabs);
    m_plotWidget = new PlotWidget(m_repository.get(), tabs);
    m_debugWidget = new DebugWidget(m_socketManager.get(), tabs);
    tabs->addTab(m_watchWidget, tr("Live Monitor"));
    tabs->addTab(m_plotWidget, tr("Plot View"));
    tabs->addTab(m_debugWidget, tr("Debug Console"));

    auto *layout = new QVBoxLayout();
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(tabs);
    if (ui->centralwidget) {
        ui->centralwidget->setLayout(layout);
    } else {
        auto *central = new QWidget(this);
        central->setLayout(layout);
        setCentralWidget(central);
    }

    connect(m_socketManager.get(), &SocketManager::dataFrame, this, [this](const QByteArray &payload) {
        if (!m_repository) {
            return;
        }
        m_repository->ingestPacket(payload);
    });
}

MainWindow::~MainWindow()
{
    delete ui;
}
