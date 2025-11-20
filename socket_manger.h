#ifndef SOCKET_MANGER_H
#define SOCKET_MANGER_H

#include <QObject>

#include <QByteArray>
#include <QHostAddress>
#include <QString>

#include <memory>

class QSerialPort;
class QTcpSocket;
class QUdpSocket;

class SocketManager : public QObject
{
    Q_OBJECT

public:
    enum class SourceType
    {
        Serial,
        Tcp,
        Udp
    };

    struct SerialSettings
    {
        QString portName;
        int baudRate = 115200;
    };

    struct TcpSettings
    {
        QString host;
        quint16 port = 0;
    };

    struct UdpSettings
    {
        QString remoteHost;
        quint16 remotePort = 0;
        quint16 localPort = 0;
    };

    explicit SocketManager(QObject *parent = nullptr);
    ~SocketManager() override;

    bool isConnected() const { return m_connected; }
    SourceType currentSource() const { return m_currentSource; }

    bool sendPayload(const QByteArray &payload, QString *errorMessage = nullptr);

public slots:
    void connectSerial(const SerialSettings &settings);
    void connectTcp(const TcpSettings &settings);
    void connectUdp(const UdpSettings &settings);
    void disconnectCurrent();

signals:
    void dataFrame(const QByteArray &payload);
    void statusChanged(const QString &message, bool error);
    void connectionStateChanged(bool connected);

private:
    class JsonFrameExtractor;

    void handleSerialReadyRead();
    void handleTcpReadyRead();
    void handleUdpReadyRead();
    void handleIoError(const QString &message);
    void resetTransports();
    void resetFrameExtractor();
    void emitFrames(const QByteArray &chunk);

    std::unique_ptr<JsonFrameExtractor> m_frameExtractor;
    std::unique_ptr<QSerialPort> m_serial;
    std::unique_ptr<QTcpSocket> m_tcp;
    std::unique_ptr<QUdpSocket> m_udp;
    bool m_connected = false;
    SourceType m_currentSource = SourceType::Serial;
    QString m_lastEndpoint;
    QString m_udpRemoteHost;
    quint16 m_udpRemotePort = 0;
    QHostAddress m_udpLastSender;
    quint16 m_udpLastSenderPort = 0;
};

#endif // SOCKET_MANGER_H
