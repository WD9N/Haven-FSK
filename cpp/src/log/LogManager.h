#pragma once
#include "LogEntry.h"
#include <QObject>
#include <QVector>

class LogManager : public QObject
{
    Q_OBJECT
public:
    explicit LogManager(QObject *parent = nullptr);

    void addEntry(const LogEntry& entry);
    const QVector<LogEntry>& entries() const { return m_entries; }
    bool saveToFile(const QString& path);
    bool loadFromFile(const QString& path);

signals:
    void entryAdded(const LogEntry& entry);

private:
    QVector<LogEntry> m_entries;
};
