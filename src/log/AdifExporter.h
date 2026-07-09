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
// Combined POTA+SOTA: POTA files carry each contact's own MY_SOTA_REF,
// and the SOTA file is generated as well (it was previously suppressed
// whenever POTA refs existed the same day).
//
// P2P: a QSO with a station at N parks is written as N records, one park
// per SIG_INFO — POTA's dedup keys on SIG_INFO, and a multi-ref value
// forfeits P2P credits (docs.pota.app park_2_park).
// MODE/SUBMODE come from the logged row (MFSK/HAVEN-FSK or PSK/PSK31);
// legacy MODE=DIGITAL rows are remapped at export time (see makeRecord).

class AdifExporter
{
public:
    static QStringList exportDate(
        const QList<QVariantMap>& contacts,
        const QString& exportPath,
        const QString& dateUtc);

private:
    // One ADIF record; theirPotaRef is a single park (or empty).
    static QString makeRecord(const QVariantMap& contact,
                               const QString& myPotaRef,
                               const QString& mySotaRef,
                               const QString& theirPotaRef);

    // All records for one contact — duplicates the record once per park
    // the other station was at (see P2P note above).
    static QString makeRecords(const QVariantMap& contact,
                                const QString& myPotaRef = QString(),
                                const QString& mySotaRef = QString());

    // Cabrillo log for ARRL Field Day — generated when any contact of the
    // day carries a received FD exchange (their_fd). ARRL's submission
    // applet takes a Cabrillo log (or dupe sheet), not ADIF.
    // Filename: {callsign}-{YYYYMMDD}-FD.cab. Returns the written path,
    // or empty when no FD contacts exist / the write failed.
    static QString exportFieldDayCabrillo(const QList<QVariantMap>& contacts,
                                           const QString& exportPath,
                                           const QString& dateUtc,
                                           const QString& myCall);

    static QString makeHeader(const QString& description);

    static QString field(const QString& name, const QString& value);

    // "W7W/SE-001" -> "W7W-SE-001"
    static QString sanitizeRef(const QString& ref);

    static QString hzToMhz(uint64_t hz);
    static QString adifDate(const QString& dateUtc);
    static QString adifTime(const QString& timeUtc);

    static bool writeFile(const QString& path, const QString& content);
};
