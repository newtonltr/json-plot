#include "datarepository.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QtGlobal>

#include <limits>

namespace {

// Format numeric values by trimming trailing zeros while preserving precision.

QString formatNumeric(double value)
{
    QString text = QString::number(value, 'f', 6);
    if (text.contains('.')) {
        while (text.endsWith('0')) {
            text.chop(1);
        }
        if (text.endsWith('.')) {
            text.chop(1);
        }
    }
    if (text.isEmpty()) {
        text = QStringLiteral("0");
    }
    return text;
}

// Try to coerce the "Stamp" field to an unsigned integer.
bool readStamp(const QJsonValue &value, quint64 &stamp)
{
    if (value.isDouble()) {
        const double raw = value.toDouble();
        if (raw < 0) {
            return false;
        }
        stamp = static_cast<quint64>(raw);
        return true;
    }
    if (value.isString()) {
        bool ok = false;
        const quint64 parsed = value.toString().toULongLong(&ok);
        if (ok) {
            stamp = parsed;
            return true;
        }
    }
    if (value.isBool()) {
        stamp = value.toBool() ? 1u : 0u;
        return true;
    }
    return false;
}

} // namespace

DataRepository::DataRepository(QObject *parent)
    : QObject(parent)
{
}

const DataRepository::VariableEntry *DataRepository::entry(const QString &name) const
{
    const auto it = m_entries.constFind(name);
    if (it == m_entries.cend()) {
        return nullptr;
    }
    return &(*it);
}

// Parse a JSON payload and update the variable repository state.
bool DataRepository::ingestPacket(const QByteArray &payload)
{
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        emit packetRejected(tr("Failed to parse JSON: %1").arg(error.errorString()));
        return false;
    }

    const QJsonObject root = document.object();
    if (!root.contains(QStringLiteral("Stamp")) || !root.contains(QStringLiteral("Data"))) {
        emit packetRejected(tr("JSON is missing the Stamp or Data fields"));
        return false;
    }

    quint64 stamp = 0;
    if (!readStamp(root.value(QStringLiteral("Stamp")), stamp)) {
        emit packetRejected(tr("Unable to interpret the Stamp field as an integer"));
        return false;
    }

    const QJsonValue dataValue = root.value(QStringLiteral("Data"));
    if (!dataValue.isObject()) {
        emit packetRejected(tr("Data field must be a JSON object"));
        return false;
    }

    const QJsonObject dataObject = dataValue.toObject();
    QStringList updatedKeys;
    QStringList newKeys;
    const quint64 packetIndex = m_totalPackets + 1;

    for (auto it = dataObject.constBegin(); it != dataObject.constEnd(); ++it) {
        const ParsedValue parsed = parseValue(it.value());
        upsertEntry(it.key(), parsed, packetIndex, newKeys);
        updatedKeys.append(it.key());
    }

    m_totalPackets = packetIndex;
    m_lastStamp = stamp;

    emit stampChanged(stamp);
    emit packetAccepted(updatedKeys);
    if (!newKeys.isEmpty()) {
        emit variablesChanged(m_variableOrder);
    }

    return true;
}

// Convert a JSON value into a normalized ParsedValue entry.
DataRepository::ParsedValue DataRepository::parseValue(const QJsonValue &value) const
{
    ParsedValue parsed;
    if (value.isDouble()) {
        parsed.numeric = true;
        parsed.numericValue = value.toDouble();
        parsed.displayValue = formatNumeric(parsed.numericValue);
        parsed.raw = parsed.numericValue;
    } else if (value.isBool()) {
        parsed.numeric = true;
        parsed.numericValue = value.toBool() ? 1.0 : 0.0;
        parsed.displayValue = parsed.numericValue > 0 ? QStringLiteral("1") : QStringLiteral("0");
        parsed.raw = value.toBool();
    } else if (value.isString()) {
        parsed.numeric = false;
        parsed.displayValue = QStringLiteral("invalid");
        parsed.invalidReason = value.toString();
        parsed.raw = value.toString();
    } else if (value.isNull()) {
        parsed.numeric = false;
        parsed.displayValue = QStringLiteral("invalid");
        parsed.invalidReason = QStringLiteral("null");
        parsed.raw = QVariant();
    } else {
        parsed.numeric = false;
        parsed.displayValue = QStringLiteral("invalid");
        parsed.invalidReason = QStringLiteral("unsupported type");
        parsed.raw = value.toVariant();
    }
    return parsed;
}

// Insert a new variable or refresh an existing one with the latest reading.
void DataRepository::upsertEntry(const QString &name, const ParsedValue &value, quint64 packetIndex, QStringList &newKeys)
{
    auto it = m_entries.find(name);
    if (it == m_entries.end()) {
        VariableEntry entry;
        entry.name = name;
        it = m_entries.insert(name, entry);
        m_variableOrder.append(name);
        newKeys.append(name);
    }

    VariableEntry &entry = it.value();
    entry.displayValue = value.displayValue;
    entry.rawValue = value.raw;
    entry.numeric = value.numeric;
    entry.numericValue = value.numericValue;
    entry.lastPacketIndex = packetIndex;
    entry.lastInvalidReason = value.invalidReason;

    if (value.numeric) {
        if (!entry.hasNumericHistory) {
            entry.minValue = entry.maxValue = value.numericValue;
            entry.hasNumericHistory = true;
        } else {
            entry.minValue = qMin(entry.minValue, value.numericValue);
            entry.maxValue = qMax(entry.maxValue, value.numericValue);
        }
    }
}
