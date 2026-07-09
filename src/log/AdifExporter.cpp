#include "AdifExporter.h"
#include <QFile>
#include <QTextStream>
#include <QDir>
#include <QDateTime>
#include <QDebug>

QString AdifExporter::field(const QString& name, const QString& value) {
    if (value.isEmpty()) return QString();
    // Length must count the bytes actually written — writeFile() encodes
    // UTF-8, so QString::length() (UTF-16 code units) understates any
    // non-ASCII value (e.g. a name like "José"), and byte-counting ADIF
    // parsers then misread every field after it.
    return QString("<%1:%2>%3\n")
        .arg(name)
        .arg(value.toUtf8().length())
        .arg(value);
}

QString AdifExporter::sanitizeRef(const QString& ref) {
    QString s = ref;
    s.replace('/', '-');
    s.replace(' ', '-');
    return s.toUpper();
}

QString AdifExporter::hzToMhz(uint64_t hz) {
    return QString::number(static_cast<double>(hz) / 1e6, 'f', 6);
}

QString AdifExporter::adifDate(const QString& dateUtc) {
    return dateUtc;  // already YYYYMMDD
}

QString AdifExporter::adifTime(const QString& timeUtc) {
    return timeUtc;  // already HHMMSS
}

QString AdifExporter::makeHeader(const QString& description) {
    QString generated = QDateTime::currentDateTimeUtc()
                            .toString("yyyy-MM-dd hh:mm:ss UTC");
    return QString(
        "HAVEN-FSK Log Export\n"
        "%1\n"
        "Generated: %2\n"
        "HAVEN-FSK — 16-tone MFSK HF Digital Mode\n"
        "https://github.com/WD9N/Haven-FSK\n"
        "<EOH>\n\n").arg(description, generated);
}

QString AdifExporter::makeRecord(const QVariantMap& c,
                                  const QString& myPotaRef,
                                  const QString& mySotaRef,
                                  const QString& theirPotaRef)
{
    QString rec;

    rec += field("CALL",             c["their_callsign"].toString());
    rec += field("QSO_DATE",         adifDate(c["date_utc"].toString()));
    rec += field("TIME_ON",          adifTime(c["time_utc"].toString()));
    // "HF" is bandForHz()'s unknown-frequency fallback, not a member of
    // the ADIF band enumeration — emitting it makes upload sites reject
    // the record outright rather than merely lack band info.
    QString band = c["band"].toString();
    if (band != "HF")
        rec += field("BAND", band);
    // Rows written by current LogManager already hold a valid ADIF
    // MODE/SUBMODE pair. Rows from pre-v0.3 databases hold MODE=DIGITAL —
    // not a valid ADIF mode — so map those by their submode instead.
    QString submode = c["submode"].toString();
    if (submode.isEmpty()) submode = "HAVEN-FSK";
    QString mode = c["mode"].toString();
    if (mode.isEmpty() || mode == "DIGITAL")
        mode = (submode == "PSK31") ? "PSK" : "MFSK";
    rec += field("MODE",             mode);
    rec += field("SUBMODE",          submode);
    uint64_t hz = c["frequency_hz"].toULongLong();
    if (hz > 0)
        rec += field("FREQ", hzToMhz(hz));
    rec += field("STATION_CALLSIGN", c["my_callsign"].toString());
    rec += field("OPERATOR",         c["my_callsign"].toString());
    rec += field("RST_SENT",         c["rs_sent"].toString());
    rec += field("RST_RCVD",         c["rs_received"].toString());
    rec += field("GRIDSQUARE",       c["their_grid"].toString());
    rec += field("NAME",             c["their_name"].toString());
    rec += field("QTH",              c["their_qth"].toString());
    rec += field("COMMENT",          c["notes"].toString());
    rec += field("MY_GRIDSQUARE",    c["my_grid"].toString());
    rec += field("MY_STATE",         c["my_state"].toString());
    QString county = c["my_county"].toString();
    if (!county.isEmpty())
        rec += field("APP_HAVEN_MY_COUNTY", county);

    if (!myPotaRef.isEmpty()) {
        rec += field("MY_SIG",      "POTA");
        rec += field("MY_SIG_INFO", myPotaRef);
    }

    // Their park goes in regardless of whether *I* was at one — a hunter's
    // log should still carry the worked park for QRZ/LoTW/records.
    if (!theirPotaRef.isEmpty()) {
        rec += field("SIG",      "POTA");
        rec += field("SIG_INFO", theirPotaRef);
    }

    if (!mySotaRef.isEmpty())
        rec += field("MY_SOTA_REF", mySotaRef);

    QString theirSota = c["their_sota_ref"].toString();
    if (!theirSota.isEmpty())
        rec += field("SOTA_REF", theirSota);

    QString fdSection = c["my_fd_section"].toString();
    if (!c["my_fd_class"].toString().isEmpty())
        rec += field("ARRL_SECT", fdSection);

    rec += "<EOR>\n\n";
    return rec;
}

QString AdifExporter::makeRecords(const QVariantMap& c,
                                   const QString& myPotaRef,
                                   const QString& mySotaRef)
{
    // P2P with a multi-park station: one record per park, single-ref
    // SIG_INFO each. POTA's dedup keys on SIG_INFO — a space-joined
    // multi-ref value (the old behavior) forfeits P2P credits.
    QStringList theirParks = c["their_pota_refs"].toString()
                                 .split(' ', Qt::SkipEmptyParts);
    if (theirParks.isEmpty())
        return makeRecord(c, myPotaRef, mySotaRef, QString());

    QString out;
    for (const QString& park : theirParks)
        out += makeRecord(c, myPotaRef, mySotaRef, park.toUpper());
    return out;
}

QStringList AdifExporter::exportDate(
    const QList<QVariantMap>& contacts,
    const QString& exportPath,
    const QString& dateUtc)
{
    QStringList createdFiles;
    if (contacts.isEmpty()) return createdFiles;
    QDir().mkpath(exportPath);

    QString myCall = contacts.first()["my_callsign"].toString().toUpper();
    if (myCall.isEmpty()) myCall = "NOCALL";

    // Collect distinct my_pota_refs and my_sota_refs
    QStringList allPotaRefs;
    QStringList allSotaRefs;
    for (const auto& c : contacts) {
        for (const QString& p :
             c["my_pota_refs"].toString().split(' ', Qt::SkipEmptyParts)) {
            if (!allPotaRefs.contains(p.toUpper()))
                allPotaRefs.append(p.toUpper());
        }
        QString sota = c["my_sota_ref"].toString().toUpper();
        if (!sota.isEmpty() && !allSotaRefs.contains(sota))
            allSotaRefs.append(sota);
    }

    // ── POTA export: one file per park reference ──────────────────────────
    for (const QString& parkRef : allPotaRefs) {
        QString content = makeHeader(
            QString("POTA Activation — %1 — %2").arg(parkRef, dateUtc));

        for (const auto& c : contacts) {
            QStringList myParks =
                c["my_pota_refs"].toString().split(' ', Qt::SkipEmptyParts);
            bool hasThisPark = false;
            for (const QString& p : myParks)
                if (p.toUpper() == parkRef) { hasThisPark = true; break; }
            if (!hasThisPark) continue;

            // Combined POTA+SOTA: each contact carries its OWN summit ref
            // (not the day's first — the operator may have moved summits).
            content += makeRecords(c, parkRef,
                                   c["my_sota_ref"].toString().toUpper());
        }

        QString filename = QString("%1@%2-%3.adi")
            .arg(myCall, parkRef, dateUtc);
        QString path = exportPath + "/" + filename;
        if (writeFile(path, content)) {
            createdFiles.append(path);
            qDebug() << "AdifExporter: wrote" << path;
        }
    }

    // ── SOTA export: one file per summit — generated even when POTA refs
    //    exist the same day (a combined activation needs BOTH uploads;
    //    this file was previously suppressed whenever POTA was present) ──
    for (const QString& sotaRef : allSotaRefs) {
        QString content = makeHeader(
            QString("SOTA Activation — %1 — %2").arg(sotaRef, dateUtc));

        for (const auto& c : contacts) {
            if (c["my_sota_ref"].toString().toUpper() != sotaRef) continue;
            QStringList myParks =
                c["my_pota_refs"].toString().split(' ', Qt::SkipEmptyParts);
            QString primaryPota = myParks.isEmpty() ? QString()
                                                    : myParks.first().toUpper();
            content += makeRecords(c, primaryPota, sotaRef);
        }

        QString filename = QString("%1-%2-%3.adi")
            .arg(myCall, sanitizeRef(sotaRef), dateUtc);
        QString path = exportPath + "/" + filename;
        if (writeFile(path, content)) {
            createdFiles.append(path);
            qDebug() << "AdifExporter: wrote" << path;
        }
    }

    // ── General ADIF — always generated ──────────────────────────────────
    {
        QString content = makeHeader(
            QString("General Log — %1 — %2 QSOs")
            .arg(dateUtc).arg(contacts.size()));

        for (const auto& c : contacts) {
            QStringList myParks =
                c["my_pota_refs"].toString().split(' ', Qt::SkipEmptyParts);
            QString primaryPota = myParks.isEmpty() ? QString()
                                                    : myParks.first().toUpper();
            QString mySota      = c["my_sota_ref"].toString().toUpper();
            content += makeRecords(c, primaryPota, mySota);
        }

        QString filename = QString("%1-%2.adi").arg(myCall, dateUtc);
        QString path     = exportPath + "/" + filename;
        if (writeFile(path, content)) {
            createdFiles.append(path);
            qDebug() << "AdifExporter: wrote" << path;
        }
    }

    return createdFiles;
}

bool AdifExporter::writeFile(const QString& path, const QString& content) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "AdifExporter: cannot write" << path << f.errorString();
        return false;
    }
    QTextStream out(&f);
    out.setEncoding(QStringConverter::Utf8);
    out << content;
    return true;
}
