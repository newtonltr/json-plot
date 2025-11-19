#include "socket_manger.h"

#include <QtSerialPort/QSerialPort>
#include <QtSerialPort/QSerialPortInfo>
#include <QTcpSocket>
#include <QUdpSocket>
#include <QAbstractSocket>
#include <QHostAddress>
#include <QIODevice>
#include <QList>

// Scan byte streams for balanced JSON objects so higher layers receive full frames.
class SocketManager::JsonFrameExtractor
{
public:
    QList<QByteArray> consume(const QByteArray &chunk)
    {
        QList<QByteArray> frames;
        for (char ch : chunk) {
            if (!m_collecting) {
                if (ch == '{') {
                    m_collecting = true;
                    m_buffer.clear();
                    m_braceDepth = 1;
                    m_inString = false;
                    m_escape = false;
                    m_buffer.append(ch);
                }
                continue;
            }

            m_buffer.append(ch);

            if (m_inString) {
                if (m_escape) {
                    m_escape = false;
                } else if (ch == '\\') {
                    m_escape = true;
                } else if (ch == '"') {
                    m_inString = false;
                }
                continue;
            }

            if (ch == '"') {
                m_inString = true;
                continue;
            }

            if (ch == '{') {
                ++m_braceDepth;
            } else if (ch == '}') {
                if (m_braceDepth > 0) {
                    --m_braceDepth;
                }
                if (m_braceDepth == 0) {
                    frames.append(m_buffer);
                    m_buffer.clear();
                    m_collecting = false;
                }
            }
        }

        return frames;
    }

    void reset()
    {
        m_buffer.clear();
        m_collecting = false;
        m_braceDepth = 0;
        m_inString = false;
        m_escape = false;
    }

private:
    QByteArray m_buffer;
    bool m_collecting = false;
    int m_braceDepth = 0;
    bool m_inString = false;
    bool m_escape = false;
};

SocketManager::SocketManager(QObject *parent)
    : QObject(parent)
    , m_frameExtractor(std::make_unique<JsonFrameExtractor>())
{
}

SocketManager::~SocketManager()
{
    resetTransports();
}

// Connect to a serial port based on the provided settings.
void SocketManager::connectSerial(const SerialSettings &settings)
{
    resetTransports();

    if (settings.portName.isEmpty()) {
        emit statusChanged(tr("Serial port is not selected"), true);
        return;
    }

    auto port = std::make_unique<QSerialPort>(this);
    port->setPortName(settings.portName);
    port->setBaudRate(settings.baudRate);
    port->setDataBits(QSerialPort::Data8);
    port->setParity(QSerialPort::NoParity);
    port->setStopBits(QSerialPort::OneStop);
    port->setFlowControl(QSerialPort::NoFlowControl);

    if (!port->open(QIODevice::ReadOnly)) {
        emit statusChanged(tr("Failed to open serial port: %1").arg(port->errorString()), true);
        return;
    }

    connect(port.get(), &QSerialPort::readyRead, this, &SocketManager::handleSerialReadyRead);
    connect(port.get(), &QSerialPort::errorOccurred, this, [this](QSerialPort::SerialPortError error) {
        if (error == QSerialPort::NoError) {
            return;
        }
        handleIoError(tr("Serial port error: %1").arg(m_serial ? m_serial->errorString() : QString{}));
    });

    m_serial = std::move(port);
    m_connected = true;
    m_currentSource = SourceType::Serial;
    m_lastEndpoint = settings.portName;

    emit connectionStateChanged(true);
    emit statusChanged(tr("Serial connection established: %1").arg(settings.portName), false);
}

// Establish a TCP connection to the lower device.
void SocketManager::connectTcp(const TcpSettings &settings)
{
    resetTransports();

    if (settings.port == 0) {
        emit statusChanged(tr("Invalid TCP port"), true);
        return;
    }

    auto socket = std::make_unique<QTcpSocket>(this);
    connect(socket.get(), &QTcpSocket::connected, this, [this]() {
        m_connected = true;
        emit connectionStateChanged(true);
        emit statusChanged(tr("TCP connection established: %1").arg(m_lastEndpoint), false);
    });
    connect(socket.get(), &QTcpSocket::readyRead, this, &SocketManager::handleTcpReadyRead);
    connect(socket.get(), &QTcpSocket::disconnected, this, [this]() {
        handleIoError(tr("TCP connection disconnected"));
    });
    connect(socket.get(), &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        handleIoError(tr("TCP error: %1").arg(m_tcp ? m_tcp->errorString() : QString{}));
    });

    m_tcp = std::move(socket);
    m_currentSource = SourceType::Tcp;
    const QString host = settings.host.isEmpty() ? QStringLiteral("127.0.0.1") : settings.host;
    m_lastEndpoint = QStringLiteral("%1:%2").arg(host).arg(settings.port);
    m_tcp->connectToHost(host, settings.port);
    emit statusChanged(tr("Connecting to TCP: %1").arg(m_lastEndpoint), false);
}

// Bind to a UDP port to accept datagrams from the controller.
void SocketManager::connectUdp(const UdpSettings &settings)
{
    resetTransports();

    if (settings.localPort == 0) {
        emit statusChanged(tr("Invalid UDP local port"), true);
        return;
    }

    auto socket = std::make_unique<QUdpSocket>(this);
    if (!socket->bind(QHostAddress::AnyIPv4, settings.localPort, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
        emit statusChanged(tr("UDP bind failed: %1").arg(socket->errorString()), true);
        return;
    }

    connect(socket.get(), &QUdpSocket::readyRead, this, &SocketManager::handleUdpReadyRead);
    connect(socket.get(), &QUdpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        handleIoError(tr("UDP error: %1").arg(m_udp ? m_udp->errorString() : QString{}));
    });

    m_udp = std::move(socket);
    m_connected = true;
    m_currentSource = SourceType::Udp;
    m_lastEndpoint = QStringLiteral("UDP(%1)").arg(settings.localPort);

    emit connectionStateChanged(true);
    emit statusChanged(tr("UDP listening on local port %1").arg(settings.localPort), false);
}

// Close whichever transport is currently open.
void SocketManager::disconnectCurrent()
{
    if (!m_connected && !m_serial && !m_tcp && !m_udp) {
        return;
    }
    resetTransports();
    emit statusChanged(tr("Connection closed"), false);
}

// Forward any pending serial bytes into the JSON extractor.
void SocketManager::handleSerialReadyRead()
{
    if (!m_serial) {
        return;
    }
    emitFrames(m_serial->readAll());
}

// Forward bytes from the TCP socket.
void SocketManager::handleTcpReadyRead()
{
    if (!m_tcp) {
        return;
    }
    emitFrames(m_tcp->readAll());
}

// Read datagrams and forward each one directly as a payload.
void SocketManager::handleUdpReadyRead()
{
    if (!m_udp) {
        return;
    }
    while (m_udp->hasPendingDatagrams()) {
        QByteArray buffer;
        buffer.resize(int(m_udp->pendingDatagramSize()));
        m_udp->readDatagram(buffer.data(), buffer.size());
        emit dataFrame(buffer);
    }
}

// Centralized error handler that updates state and notifies the UI.
void SocketManager::handleIoError(const QString &message)
{
    emit statusChanged(message, true);
    if (m_connected) {
        m_connected = false;
        emit connectionStateChanged(false);
    }
    resetTransports();
}

// Tear down each transport safely before switching to another source.
void SocketManager::resetTransports()
{
    if (m_serial) {
        m_serial->close();
        m_serial.reset();
    }
    if (m_tcp) {
        m_tcp->abort();
        m_tcp.reset();
    }
    if (m_udp) {
        m_udp->close();
        m_udp.reset();
    }

    if (m_connected) {
        m_connected = false;
        emit connectionStateChanged(false);
    }

    resetFrameExtractor();
}

// Clear the JSON frame extraction buffers.
void SocketManager::resetFrameExtractor()
{
    if (m_frameExtractor) {
        m_frameExtractor->reset();
    }
}

// Pass any completed JSON frames upward to listeners.
void SocketManager::emitFrames(const QByteArray &chunk)
{
    if (!m_frameExtractor) {
        return;
    }
    const QList<QByteArray> frames = m_frameExtractor->consume(chunk);
    for (const QByteArray &frame : frames) {
        emit dataFrame(frame);
    }
}
