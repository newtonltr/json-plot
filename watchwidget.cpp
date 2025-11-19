#include "watchwidget.h"

#include "datarepository.h"
#include "socket_manger.h"

#include <QtSerialPort/QSerialPortInfo>

#include <QAbstractItemView>
#include <QColor>
#include <QComboBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPalette>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVariant>
#include <QVBoxLayout>

namespace {

constexpr int kColumnName = 0;
constexpr int kColumnValue = 1;

} // namespace

// Widget that shows live data from the repository and connection controls.
WatchWidget::WatchWidget(DataRepository *store, SocketManager *manager, QWidget *parent)
    : QWidget(parent)
    , m_store(store)
    , m_manager(manager)
{
    buildUi();
    refreshSerialPorts();
    updateCounters();

    if (m_store) {
        connect(m_store, &DataRepository::packetAccepted, this, &WatchWidget::handlePacketAccepted);
        connect(m_store, &DataRepository::packetRejected, this, &WatchWidget::handlePacketRejected);
        connect(m_store, &DataRepository::stampChanged, this, &WatchWidget::handleStampChanged);
    }
    if (m_manager) {
        connect(m_manager, &SocketManager::connectionStateChanged, this, &WatchWidget::handleConnectionStateChanged);
        connect(m_manager, &SocketManager::statusChanged, this, &WatchWidget::handleStatusMessage);
    }
}

// Build the high-level UI layout for the monitor tab.
void WatchWidget::buildUi()
{
    auto *rootLayout = new QVBoxLayout(this);

    auto *connectionBox = new QGroupBox(tr("Data Source"), this);
    auto *connectionLayout = new QGridLayout(connectionBox);

    m_sourceCombo = new QComboBox(connectionBox);
    m_sourceCombo->addItem(tr("Serial"));
    m_sourceCombo->addItem(tr("TCP"));
    m_sourceCombo->addItem(tr("UDP"));

    m_sourceStack = new QStackedWidget(connectionBox);
    m_sourceStack->addWidget(buildSerialPage());
    m_sourceStack->addWidget(buildTcpPage());
    m_sourceStack->addWidget(buildUdpPage());

    connect(m_sourceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), m_sourceStack, &QStackedWidget::setCurrentIndex);

    m_connectButton = new QPushButton(tr("Connect"), connectionBox);
    connect(m_connectButton, &QPushButton::clicked, this, &WatchWidget::handleConnectClicked);

    connectionLayout->addWidget(new QLabel(tr("Source"), connectionBox), 0, 0);
    connectionLayout->addWidget(m_sourceCombo, 0, 1);
    connectionLayout->addWidget(m_sourceStack, 1, 0, 1, 2);
    connectionLayout->addWidget(m_connectButton, 0, 2);

    connectionLayout->setColumnStretch(1, 1);
    connectionLayout->setColumnMinimumWidth(2, 120);

    rootLayout->addWidget(connectionBox);

    auto *infoLayout = new QHBoxLayout();
    m_stampLabel = new QLabel(tr("-"), this);
    m_packetLabel = new QLabel(tr("0"), this);
    m_statusLabel = new QLabel(tr("Waiting for data"), this);
    m_statusLabel->setStyleSheet(QStringLiteral("color:#555555;"));

    infoLayout->addWidget(new QLabel(tr("Stamp:"), this));
    infoLayout->addWidget(m_stampLabel);
    infoLayout->addSpacing(20);
    infoLayout->addWidget(new QLabel(tr("Parsed packets:"), this));
    infoLayout->addWidget(m_packetLabel);
    infoLayout->addStretch();
    infoLayout->addWidget(m_statusLabel);

    rootLayout->addLayout(infoLayout);

    m_table = new QTableWidget(0, 2, this);
    m_table->setHorizontalHeaderLabels({tr("Variable"), tr("Value (decimal)")});
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->horizontalHeader()->setSectionResizeMode(kColumnName, QHeaderView::ResizeToContents);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);

    rootLayout->addWidget(m_table, 1);
}

// Build UI controls specific to serial configuration.
QWidget *WatchWidget::buildSerialPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QHBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);

    m_serialPortCombo = new QComboBox(page);
    m_serialPortCombo->setMinimumWidth(150);

    auto *refreshButton = new QPushButton(tr("Refresh"), page);
    connect(refreshButton, &QPushButton::clicked, this, &WatchWidget::refreshSerialPorts);

    m_serialBaudCombo = new QComboBox(page);
    m_serialBaudCombo->setEditable(true);
    const QList<int> baudRates = {115200, 230400, 460800, 921600, 57600, 38400, 19200, 9600};
    for (int baud : baudRates) {
        m_serialBaudCombo->addItem(QString::number(baud));
    }
    m_serialBaudCombo->setCurrentText(QString::number(115200));

    layout->addWidget(new QLabel(tr("Serial Port"), page));
    layout->addWidget(m_serialPortCombo, 1);
    layout->addWidget(refreshButton);
    layout->addSpacing(15);
    layout->addWidget(new QLabel(tr("Baud Rate"), page));
    layout->addWidget(m_serialBaudCombo);
    layout->addStretch();

    return page;
}

// Build TCP configuration controls.
QWidget *WatchWidget::buildTcpPage()
{
    auto *page = new QWidget(this);
    auto *form = new QFormLayout(page);
    form->setContentsMargins(0, 0, 0, 0);

    m_tcpHostEdit = new QLineEdit(page);
    m_tcpHostEdit->setPlaceholderText(QStringLiteral("127.0.0.1"));
    m_tcpPortSpin = new QSpinBox(page);
    m_tcpPortSpin->setRange(1, 65535);
    m_tcpPortSpin->setValue(9000);

    form->addRow(tr("Server Address"), m_tcpHostEdit);
    form->addRow(tr("Port"), m_tcpPortSpin);

    return page;
}

// Build UDP configuration controls.
QWidget *WatchWidget::buildUdpPage()
{
    auto *page = new QWidget(this);
    auto *form = new QFormLayout(page);
    form->setContentsMargins(0, 0, 0, 0);

    m_udpRemoteHostEdit = new QLineEdit(page);
    m_udpRemoteHostEdit->setPlaceholderText(tr("Optional peer IP"));
    m_udpRemotePortSpin = new QSpinBox(page);
    m_udpRemotePortSpin->setRange(0, 65535);
    m_udpRemotePortSpin->setValue(0);
    m_udpLocalPortSpin = new QSpinBox(page);
    m_udpLocalPortSpin->setRange(1, 65535);
    m_udpLocalPortSpin->setValue(9001);

    form->addRow(tr("Remote Address"), m_udpRemoteHostEdit);
    form->addRow(tr("Remote Port (optional)"), m_udpRemotePortSpin);
    form->addRow(tr("Local Listen Port"), m_udpLocalPortSpin);

    return page;
}

// Whenever a valid JSON packet arrives update counters/table.
void WatchWidget::handlePacketAccepted(const QStringList &)
{
    updateCounters();
    updateVariableDisplay();
}

// Surface parse errors to the status label.
void WatchWidget::handlePacketRejected(const QString &reason)
{
    showStatus(reason, true);
}

// Display the most recent timestamp reported by the device.
void WatchWidget::handleStampChanged(quint64 stamp)
{
    m_stampLabel->setText(QString::number(stamp));
}

// Connect or disconnect based on the current transport selection.
void WatchWidget::handleConnectClicked()
{
    if (!m_manager) {
        return;
    }

    if (m_manager->isConnected()) {
        m_manager->disconnectCurrent();
        return;
    }

    switch (m_sourceCombo->currentIndex()) {
    case 0: {
        SocketManager::SerialSettings settings;
        settings.portName = m_serialPortCombo->currentText().trimmed();
        bool ok = false;
        const int baud = m_serialBaudCombo->currentText().trimmed().toInt(&ok);
        if (!ok || baud <= 0) {
            QMessageBox::warning(this, tr("Configuration Error"), tr("Please enter a valid baud rate"));
            return;
        }
        settings.baudRate = baud;
        if (settings.portName.isEmpty()) {
            QMessageBox::warning(this, tr("Configuration Error"), tr("Please select a serial port"));
            return;
        }
        m_manager->connectSerial(settings);
        break;
    }
    case 1: {
        SocketManager::TcpSettings settings;
        settings.host = m_tcpHostEdit->text().trimmed();
        settings.port = static_cast<quint16>(m_tcpPortSpin->value());
        if (settings.port == 0) {
            QMessageBox::warning(this, tr("Configuration Error"), tr("Please enter a valid TCP port"));
            return;
        }
        m_manager->connectTcp(settings);
        break;
    }
    case 2: {
        SocketManager::UdpSettings settings;
        settings.remoteHost = m_udpRemoteHostEdit->text().trimmed();
        settings.remotePort = static_cast<quint16>(m_udpRemotePortSpin->value());
        settings.localPort = static_cast<quint16>(m_udpLocalPortSpin->value());
        if (settings.localPort == 0) {
            QMessageBox::warning(this, tr("Configuration Error"), tr("Please enter a valid local UDP port"));
            return;
        }
        m_manager->connectUdp(settings);
        break;
    }
    default:
        break;
    }
}

// Update UI elements when transport state toggles.
void WatchWidget::handleConnectionStateChanged(bool connected)
{
    m_connectButton->setText(connected ? tr("Disconnect") : tr("Connect"));
    m_sourceCombo->setEnabled(!connected);
    m_sourceStack->setEnabled(!connected);
}

// Reflect status changes reported by SocketManager.
void WatchWidget::handleStatusMessage(const QString &message, bool error)
{
    showStatus(message, error);
}

// Refresh the list of serial ports to keep the combo box in sync.
void WatchWidget::refreshSerialPorts()
{
    const QString current = m_serialPortCombo ? m_serialPortCombo->currentText() : QString();
    if (!m_serialPortCombo) {
        return;
    }

    m_serialPortCombo->clear();
    const auto ports = QSerialPortInfo::availablePorts();
    for (const QSerialPortInfo &info : ports) {
        m_serialPortCombo->addItem(info.portName());
    }

    const int idx = m_serialPortCombo->findText(current);
    if (idx >= 0) {
        m_serialPortCombo->setCurrentIndex(idx);
    } else if (!ports.isEmpty()) {
        m_serialPortCombo->setCurrentIndex(0);
    }
}

// Reshape the table to mirror every known variable/value pair.
void WatchWidget::updateVariableDisplay()
{
    if (!m_store) {
        return;
    }

    const QStringList keys = m_store->orderedKeys();
    m_table->setRowCount(keys.size());
    const auto &entries = m_store->entries();

    for (int row = 0; row < keys.size(); ++row) {
        const QString &name = keys.at(row);
        const auto it = entries.constFind(name);
        QString displayValue = QStringLiteral("-");
        bool isNumeric = false;
        QString tooltip;
        if (it != entries.cend()) {
            displayValue = it.value().displayValue;
            isNumeric = it.value().numeric;
            if (!isNumeric && !it.value().lastInvalidReason.isEmpty()) {
                tooltip = tr("Raw value: %1").arg(it.value().lastInvalidReason);
            } else if (it.value().rawValue.isValid() && it.value().rawValue != QVariant()) {
                tooltip = tr("Raw value: %1").arg(it.value().rawValue.toString());
            }
        }

        QTableWidgetItem *nameItem = m_table->item(row, kColumnName);
        if (!nameItem) {
            nameItem = new QTableWidgetItem(name);
            m_table->setItem(row, kColumnName, nameItem);
        }
        nameItem->setText(name);
        nameItem->setFlags(Qt::ItemIsEnabled);

        QTableWidgetItem *valueItem = m_table->item(row, kColumnValue);
        if (!valueItem) {
            valueItem = new QTableWidgetItem(displayValue);
            m_table->setItem(row, kColumnValue, valueItem);
        } else {
            valueItem->setText(displayValue);
        }
        valueItem->setFlags(Qt::ItemIsEnabled);
        if (!tooltip.isEmpty()) {
            valueItem->setToolTip(tooltip);
        } else {
            valueItem->setToolTip(QString());
        }
        valueItem->setForeground(isNumeric ? palette().color(QPalette::WindowText) : QColor(200, 30, 30));
    }
}

// Update the packet counter text.
void WatchWidget::updateCounters()
{
    if (!m_store) {
        m_packetLabel->setText(QStringLiteral("0"));
        return;
    }
    m_packetLabel->setText(QString::number(m_store->totalPackets()));
}

// Update the status label with optional error coloring.
void WatchWidget::showStatus(const QString &message, bool error)
{
    if (!m_statusLabel) {
        return;
    }
    m_statusLabel->setText(message);
    m_statusLabel->setStyleSheet(error ? QStringLiteral("color:#b71c1c;") : QStringLiteral("color:#2e7d32;"));
}
