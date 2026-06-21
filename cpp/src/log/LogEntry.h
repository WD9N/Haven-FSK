#pragma once
#include <QString>
#include <QDateTime>

struct LogEntry {
    QDateTime timestamp;
    QString   callsign;
    QString   frequency;
    QString   mode;
    QString   notes;
};
