#ifndef WATCHWIDGET_H
#define WATCHWIDGET_H

#include <QWidget>
#include <QtGlobal>

class QComboBox;
class QLineEdit;
class QPushButton;
class QLabel;
class QSpinBox;
class QStackedWidget;
class QTableWidget;

class DataRepository;
class SocketManager;

class WatchWidget : public QWidget
{
    Q_OBJECT
public:
    explicit WatchWidget(DataRepository *store, SocketManager *manager, QWidget *parent = nullptr);

private slots:
    void handlePacketAccepted(const QStringList &keys);
    void handlePacketRejected(const QString &reason);
    void handleStampChanged(quint64 stamp);
    void handleConnectClicked();
    void handleConnectionStateChanged(bool connected);
    void handleStatusMessage(const QString &message, bool error);
    void refreshSerialPorts();

private:
    void buildUi();
    QWidget *buildSerialPage();
    QWidget *buildTcpPage();
    QWidget *buildUdpPage();
    void updateVariableDisplay();
    void updateCounters();
    void showStatus(const QString &message, bool error);

    DataRepository *m_store = nullptr;
    SocketManager *m_manager = nullptr;

    QComboBox *m_sourceCombo = nullptr;
    QStackedWidget *m_sourceStack = nullptr;

    QComboBox *m_serialPortCombo = nullptr;
    QComboBox *m_serialBaudCombo = nullptr;
    QLineEdit *m_tcpHostEdit = nullptr;
    QSpinBox *m_tcpPortSpin = nullptr;
    QLineEdit *m_udpRemoteHostEdit = nullptr;
    QSpinBox *m_udpRemotePortSpin = nullptr;
    QSpinBox *m_udpLocalPortSpin = nullptr;

    QPushButton *m_connectButton = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_stampLabel = nullptr;
    QLabel *m_packetLabel = nullptr;
    QTableWidget *m_table = nullptr;
};

#endif // WATCHWIDGET_H
