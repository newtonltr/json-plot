#ifndef DATAREPOSITORY_H
#define DATAREPOSITORY_H

#include <QObject>
#include <QHash>
#include <QJsonObject>
#include <QStringList>
#include <QVariant>

#include <optional>

class DataRepository : public QObject
{
    Q_OBJECT

public:
    struct VariableEntry
    {
        QString name;
        QString displayValue;
        QVariant rawValue;
        bool numeric = false;
        double numericValue = 0.0;
        double minValue = 0.0;
        double maxValue = 0.0;
        bool hasNumericHistory = false;
        quint64 lastPacketIndex = 0;
        QString lastInvalidReason;
    };

    explicit DataRepository(QObject *parent = nullptr);

    bool ingestPacket(const QByteArray &payload);

    const QHash<QString, VariableEntry> &entries() const { return m_entries; }
    const VariableEntry *entry(const QString &name) const;
    QStringList orderedKeys() const { return m_variableOrder; }

    quint64 totalPackets() const { return m_totalPackets; }
    quint64 lastStamp() const { return m_lastStamp; }

signals:
    void packetAccepted(const QStringList &updatedKeys);
    void packetRejected(const QString &reason);
    void stampChanged(quint64 stamp);
    void variablesChanged(const QStringList &names);

private:
    struct ParsedValue
    {
        bool numeric = false;
        double numericValue = 0.0;
        QString displayValue;
        QString invalidReason;
        QVariant raw;
    };

    ParsedValue parseValue(const QJsonValue &value) const;
    void upsertEntry(const QString &name, const ParsedValue &value, quint64 packetIndex, QStringList &newKeys);

    QHash<QString, VariableEntry> m_entries;
    QStringList m_variableOrder;
    quint64 m_totalPackets = 0;
    quint64 m_lastStamp = 0;
};

#endif // DATAREPOSITORY_H
