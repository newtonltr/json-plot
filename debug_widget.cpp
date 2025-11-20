#include "debug_widget.h"

#include <QDateTime>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QStringList>
#include <QString>
#include <QTextCursor>
#include <QVBoxLayout>

namespace {

QString formatTimestamp(const QDateTime &timestamp)
{
    return timestamp.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
}

QString formatHex(const QByteArray &payload)
{
    const QByteArray hex = payload.toHex(' ').toUpper();
    return QString::fromLatin1(hex);
}

} // namespace

DebugWidget::DebugWidget(SocketManager *manager, QWidget *parent)
    : QWidget(parent)
    , m_manager(manager)
{
    buildUi();
    updateModeButtons();
    showStatus(tr("Disconnected"), true);
    if (m_statusLabel) {
        m_statusLabel->setStyleSheet(QStringLiteral("color:#555555;"));
    }

    if (m_manager) {
        connect(m_manager, &SocketManager::dataFrame, this, &DebugWidget::handleIncomingFrame);
        connect(m_manager, &SocketManager::connectionStateChanged, this, &DebugWidget::handleConnectionStateChanged);
        handleConnectionStateChanged(m_manager->isConnected());
    } else if (m_sendButton) {
        m_sendButton->setEnabled(false);
    }
}

void DebugWidget::buildUi()
{
    auto *root = new QVBoxLayout(this);

    auto *trafficBox = new QGroupBox(tr("Traffic Monitor"), this);
    auto *trafficLayout = new QVBoxLayout(trafficBox);
    auto *trafficControls = new QHBoxLayout();
    m_logFormatButton = new QPushButton(trafficBox);
    m_clearButton = new QPushButton(tr("Clear"), trafficBox);
    trafficControls->addWidget(m_logFormatButton);
    trafficControls->addStretch();
    trafficControls->addWidget(m_clearButton);
    trafficLayout->addLayout(trafficControls);

    m_logView = new QPlainTextEdit(trafficBox);
    m_logView->setReadOnly(true);
    trafficLayout->addWidget(m_logView, 1);

    root->addWidget(trafficBox, 1);

    auto *sendBox = new QGroupBox(tr("Send Data"), this);
    auto *sendLayout = new QVBoxLayout(sendBox);

    m_sendEdit = new QPlainTextEdit(sendBox);
    m_sendEdit->setPlaceholderText(tr("Enter payload to send..."));
    sendLayout->addWidget(m_sendEdit, 1);

    auto *sendControls = new QHBoxLayout();
    m_sendFormatButton = new QPushButton(sendBox);
    m_sendButton = new QPushButton(tr("Send"), sendBox);
    m_statusLabel = new QLabel(tr("Disconnected"), sendBox);
    m_statusLabel->setStyleSheet(QStringLiteral("color:#555555;"));

    sendControls->addWidget(m_sendFormatButton);
    sendControls->addStretch();
    sendControls->addWidget(m_statusLabel);
    sendControls->addWidget(m_sendButton);
    sendLayout->addLayout(sendControls);

    root->addWidget(sendBox);

    connect(m_logFormatButton, &QPushButton::clicked, this, &DebugWidget::handleLogFormatToggled);
    connect(m_sendFormatButton, &QPushButton::clicked, this, &DebugWidget::handleSendFormatToggled);
    connect(m_sendButton, &QPushButton::clicked, this, &DebugWidget::handleSendClicked);
    connect(m_clearButton, &QPushButton::clicked, this, &DebugWidget::handleClearLog);
}

void DebugWidget::updateModeButtons()
{
    if (m_logFormatButton) {
        m_logFormatButton->setText(m_displayHex ? tr("Show as Text") : tr("Show as Hex"));
    }
    if (m_sendFormatButton) {
        m_sendFormatButton->setText(m_sendHex ? tr("Input as Text") : tr("Input as Hex"));
    }
}

void DebugWidget::handleIncomingFrame(const QByteArray &payload)
{
    appendLogEntry(true, payload);
}

void DebugWidget::handleConnectionStateChanged(bool connected)
{
    if (m_sendButton) {
        m_sendButton->setEnabled(connected);
    }
    if (!connected) {
        showStatus(tr("Disconnected"), false);
        if (m_statusLabel) {
            m_statusLabel->setStyleSheet(QStringLiteral("color:#555555;"));
        }
    } else {
        showStatus(tr("Ready"), false);
    }
}

void DebugWidget::handleSendClicked()
{
    if (!m_manager) {
        showStatus(tr("Socket manager unavailable"), true);
        return;
    }

    QByteArray payload;
    QString error;
    if (!parseSendPayload(payload, error)) {
        showStatus(error, true);
        return;
    }

    QString sendError;
    if (!m_manager->sendPayload(payload, &sendError)) {
        showStatus(sendError.isEmpty() ? tr("Failed to send payload") : sendError, true);
        return;
    }

    appendLogEntry(false, payload);
    showStatus(tr("Payload sent"), false);
}

void DebugWidget::handleLogFormatToggled()
{
    m_displayHex = !m_displayHex;
    updateModeButtons();
    refreshLogView();
}

void DebugWidget::handleSendFormatToggled()
{
    m_sendHex = !m_sendHex;
    updateModeButtons();
}

void DebugWidget::handleClearLog()
{
    m_entries.clear();
    if (m_logView) {
        m_logView->clear();
    }
}

void DebugWidget::appendLogEntry(bool incoming, const QByteArray &payload)
{
    LogEntry entry;
    entry.incoming = incoming;
    entry.payload = payload;
    entry.timestamp = QDateTime::currentDateTime();
    entry.source = m_manager ? m_manager->currentSource() : SocketManager::SourceType::Serial;
    m_entries.append(entry);

    if (m_entries.size() > kMaxEntries) {
        const int toTrim = m_entries.size() - kMaxEntries;
        m_entries.erase(m_entries.begin(), m_entries.begin() + toTrim);
    }

    refreshLogView();
}

void DebugWidget::refreshLogView()
{
    if (!m_logView) {
        return;
    }

    QStringList lines;
    lines.reserve(m_entries.size());
    for (const LogEntry &entry : m_entries) {
        lines.append(formatEntry(entry));
    }

    const QSignalBlocker blocker(m_logView);
    m_logView->setPlainText(lines.join(QLatin1Char('\n')));
    m_logView->moveCursor(QTextCursor::End);
    QScrollBar *bar = m_logView->verticalScrollBar();
    if (bar) {
        bar->setValue(bar->maximum());
    }
}

QString DebugWidget::formatEntry(const LogEntry &entry) const
{
    const QString ts = formatTimestamp(entry.timestamp);
    const QString direction = entry.incoming ? tr("Received") : tr("Sent");
    const QString source = sourceToString(entry.source);
    const QString payload = formatPayload(entry.payload, m_displayHex);
    return QStringLiteral("[%1] %2 (%3): %4").arg(ts, direction, source, payload);
}

QString DebugWidget::formatPayload(const QByteArray &payload, bool hex) const
{
    if (hex) {
        return formatHex(payload);
    }
    QString text = QString::fromUtf8(payload);
    if (text.isEmpty() && !payload.isEmpty()) {
        text = QString::fromLatin1(payload);
    }
    return text;
}

QString DebugWidget::sourceToString(SocketManager::SourceType source) const
{
    switch (source) {
    case SocketManager::SourceType::Serial:
        return QStringLiteral("Serial");
    case SocketManager::SourceType::Tcp:
        return QStringLiteral("TCP");
    case SocketManager::SourceType::Udp:
        return QStringLiteral("UDP");
    }
    return QStringLiteral("Unknown");
}

bool DebugWidget::parseSendPayload(QByteArray &payload, QString &error) const
{
    if (!m_sendEdit) {
        error = tr("No input widget available");
        return false;
    }

    const QString text = m_sendEdit->toPlainText().trimmed();
    if (text.isEmpty()) {
        error = tr("Input is empty");
        return false;
    }

    if (m_sendHex) {
        payload = parseHexString(text, error);
        return error.isEmpty();
    }

    payload = text.toUtf8();
    return true;
}

QByteArray DebugWidget::parseHexString(const QString &text, QString &error) const
{
    QString cleaned;
    cleaned.reserve(text.size());
    for (QChar ch : text) {
        if (!ch.isSpace()) {
            cleaned.append(ch);
        }
    }

    if (cleaned.isEmpty()) {
        error = tr("Hex input is empty");
        return {};
    }
    if (cleaned.size() % 2 != 0) {
        error = tr("Hex data must contain pairs of characters");
        return {};
    }

    QByteArray payload;
    payload.reserve(cleaned.size() / 2);
    for (int i = 0; i < cleaned.size(); i += 2) {
        bool ok = false;
        const int value = cleaned.mid(i, 2).toInt(&ok, 16);
        if (!ok || value < 0 || value > 0xFF) {
            error = tr("Invalid hex data near position %1").arg(i);
            return {};
        }
        payload.append(static_cast<char>(value));
    }

    error.clear();
    return payload;
}

void DebugWidget::showStatus(const QString &message, bool error)
{
    if (!m_statusLabel) {
        return;
    }
    m_statusLabel->setText(message);
    m_statusLabel->setStyleSheet(error ? QStringLiteral("color:#b71c1c;") : QStringLiteral("color:#2e7d32;"));
}
