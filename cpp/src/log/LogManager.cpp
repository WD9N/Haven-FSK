#include "LogManager.h"

LogManager::LogManager(QObject *parent) : QObject(parent) {}

void LogManager::addEntry(const LogEntry& entry)
{
    m_entries.append(entry);
    emit entryAdded(entry);
}

bool LogManager::saveToFile(const QString&) { return false; }
bool LogManager::loadFromFile(const QString&) { return false; }
