#pragma once
#include <QList>
#include <QVariantMap>
#include <QString>
#include <QStringList>
#include <cstdint>

// AdifExporter — generates ADIF files from QSO records.
//
// Auto-detects activity type from my_pota_refs and my_sota_ref fields.
// Generates per-park POTA files, optional SOTA file, and general ADIF.
//
// POTA filename:  {callsign}@{park_ref}-{YYYYMMDD}.adi
// SOTA filename:  {callsign}-{sanitized_ref}-{YYYYMMDD}.adi
// General:        {callsign}-{YYYYMMDD}.adi
//
// Combined POTA+SOTA: POTA files each contain MY_SOTA_REF.
// MODE always DIGITAL, SUBMODE always HAVEN-FSK.

class AdifExporter
{
public:
    static QStringList exportDate(
        const QList<QVariantMap>& contacts,
        const QString& exportPath,
        const QString& dateUtc);

private:
    static QString makeRecord(const QVariantMap& contact,
                               const QString& myPotaRef = QString(),
                               const QString& mySotaRef = QString());

    static QString makeHeader(const QString& description);

    static QString field(const QString& name, const QString& value);

    // "W7W/SE-001" -> "W7W-SE-001"
    static QString sanitizeRef(const QString& ref);

    static QString hzToMhz(uint64_t hz);
    static QString adifDate(const QString& dateUtc);
    static QString adifTime(const QString& timeUtc);

    static bool writeFile(const QString& path, const QString& content);
};
