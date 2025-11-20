#ifndef DEBUG_WIDGET_H
#define DEBUG_WIDGET_H

#include <QByteArray>
#include <QDateTime>
#include <QVector>
#include <QWidget>

#include "socket_manger.h"

class QPlainTextEdit;
class QPushButton;
class QLabel;

class DebugWidget : public QWidget
{
    Q_OBJECT
public:
    explicit DebugWidget(SocketManager *manager, QWidget *parent = nullptr);

private slots:
    void handleIncomingFrame(const QByteArray &payload);
    void handleConnectionStateChanged(bool connected);
    void handleSendClicked();
    void handleLogFormatToggled();
    void handleSendFormatToggled();
    void handleClearLog();

private:
    struct LogEntry
    {
        bool incoming = false;
        SocketManager::SourceType source = SocketManager::SourceType::Serial;
        QByteArray payload;
        QDateTime timestamp;
    };

    void buildUi();
    void appendLogEntry(bool incoming, const QByteArray &payload);
    void refreshLogView();
    QString formatEntry(const LogEntry &entry) const;
    QString formatPayload(const QByteArray &payload, bool hex) const;
    QString sourceToString(SocketManager::SourceType source) const;
    bool parseSendPayload(QByteArray &payload, QString &error) const;
    QByteArray parseHexString(const QString &text, QString &error) const;
    void updateModeButtons();
    void showStatus(const QString &message, bool error);

    static constexpr int kMaxEntries = 500;

    SocketManager *m_manager = nullptr;
    QPlainTextEdit *m_logView = nullptr;
    QPlainTextEdit *m_sendEdit = nullptr;
    QPushButton *m_logFormatButton = nullptr;
    QPushButton *m_sendFormatButton = nullptr;
    QPushButton *m_sendButton = nullptr;
    QPushButton *m_clearButton = nullptr;
    QLabel *m_statusLabel = nullptr;
    QVector<LogEntry> m_entries;
    bool m_displayHex = false;
    bool m_sendHex = false;
};

#endif // DEBUG_WIDGET_H
