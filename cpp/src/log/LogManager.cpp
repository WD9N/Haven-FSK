#include "LogManager.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QSqlRecord>
#include <QStandardPaths>
#include <QDir>
#include <QDebug>
#include <QDateTime>

LogManager::LogManager(QObject* parent)
    : QObject(parent)
{}

LogManager::~LogManager() {
    close();
}

QString LogManager::buildDbPath() const {
    QString dataDir = QStandardPaths::writableLocation(
        QStandardPaths::AppDataLocation);
    QDir().mkpath(dataDir);
    return dataDir + "/haven_fsk_log.db";
}

bool LogManager::open() {
    m_dbPath = buildDbPath();

    m_db = QSqlDatabase::addDatabase("QSQLITE", "haven_log");
    m_db.setDatabaseName(m_dbPath);

    if (!m_db.open()) {
        QString err = m_db.lastError().text();
        qWarning() << "LogManager: cannot open database:" << err;
        emit logError("Cannot open log database: " + err);
        return false;
    }

    QSqlQuery q(m_db);
    q.exec("PRAGMA journal_mode=WAL");
    q.exec("PRAGMA synchronous=NORMAL");

    if (!createSchema()) return false;

    m_open = true;
    qDebug() << "LogManager: database open at" << m_dbPath;
    return true;
}

void LogManager::close() {
    if (m_open) {
        m_db.close();
        m_open = false;
    }
    QSqlDatabase::removeDatabase("haven_log");
}

bool LogManager::isOpen() const {
    return m_open;
}

bool LogManager::createSchema() {
    QSqlQuery q(m_db);
    bool ok = q.exec(R"(
        CREATE TABLE IF NOT EXISTS contacts (
            id              INTEGER PRIMARY KEY AUTOINCREMENT,
            date_utc        TEXT NOT NULL,
            time_utc        TEXT NOT NULL,
            their_callsign  TEXT NOT NULL,
            rs_received     TEXT,
            rs_sent         TEXT,
            their_pota_refs TEXT,
            their_sota_ref  TEXT,
            their_grid      TEXT,
            their_name      TEXT,
            their_qth       TEXT,
            their_fd        TEXT,
            frequency_hz    INTEGER,
            band            TEXT,
            mode            TEXT DEFAULT 'DIGITAL',
            submode         TEXT DEFAULT 'HAVEN-FSK',
            notes           TEXT,
            my_callsign     TEXT,
            my_grid         TEXT,
            my_pota_refs    TEXT,
            my_sota_ref     TEXT,
            my_fd_class     TEXT,
            my_fd_section   TEXT,
            my_op_name      TEXT,
            created_at      TEXT DEFAULT (datetime('now'))
        )
    )");

    if (!ok) {
        qWarning() << "LogManager: schema creation failed:"
                   << q.lastError().text();
        emit logError("Log database schema error: " + q.lastError().text());
        return false;
    }

    q.exec("CREATE INDEX IF NOT EXISTS idx_date_utc "
           "ON contacts(date_utc)");
    q.exec("CREATE INDEX IF NOT EXISTS idx_my_pota "
           "ON contacts(date_utc, my_pota_refs)");

    return true;
}

bool LogManager::logContact(const QVariantMap& fields) {
    if (!m_open) return false;

    QStringList theirParks = fields["their_pota_refs"].toStringList();
    QStringList myParks    = fields["my_pota_refs"].toStringList();

    // Derive band from frequency
    uint64_t hz = fields["frequency_hz"].toULongLong();
    QString band;
    if      (hz >= 1800000   && hz <= 2000000)   band = "160m";
    else if (hz >= 3500000   && hz <= 4000000)   band = "80m";
    else if (hz >= 5330500   && hz <= 5403500)   band = "60m";
    else if (hz >= 7000000   && hz <= 7300000)   band = "40m";
    else if (hz >= 10100000  && hz <= 10150000)  band = "30m";
    else if (hz >= 14000000  && hz <= 14350000)  band = "20m";
    else if (hz >= 18068000  && hz <= 18168000)  band = "17m";
    else if (hz >= 21000000  && hz <= 21450000)  band = "15m";
    else if (hz >= 24890000  && hz <= 24990000)  band = "12m";
    else if (hz >= 28000000  && hz <= 29700000)  band = "10m";
    else if (hz >= 50000000  && hz <= 54000000)  band = "6m";
    else if (hz >= 144000000 && hz <= 148000000) band = "2m";
    else band = "HF";

    QSqlQuery q(m_db);
    q.prepare(R"(
        INSERT INTO contacts (
            date_utc, time_utc, their_callsign,
            rs_received, rs_sent,
            their_pota_refs, their_sota_ref,
            their_grid, their_name, their_qth, their_fd,
            frequency_hz, band, mode, submode, notes,
            my_callsign, my_grid, my_pota_refs, my_sota_ref,
            my_fd_class, my_fd_section, my_op_name
        ) VALUES (
            :date_utc, :time_utc, :their_callsign,
            :rs_received, :rs_sent,
            :their_pota_refs, :their_sota_ref,
            :their_grid, :their_name, :their_qth, :their_fd,
            :frequency_hz, :band, :mode, :submode, :notes,
            :my_callsign, :my_grid, :my_pota_refs, :my_sota_ref,
            :my_fd_class, :my_fd_section, :my_op_name
        )
    )");

    q.bindValue(":date_utc",        fields["date_utc"].toString());
    q.bindValue(":time_utc",        fields["time_utc"].toString());
    q.bindValue(":their_callsign",  fields["their_callsign"].toString().toUpper());
    q.bindValue(":rs_received",     fields["rs_received"].toString());
    q.bindValue(":rs_sent",         fields["rs_sent"].toString());
    q.bindValue(":their_pota_refs", theirParks.join(" "));
    q.bindValue(":their_sota_ref",  fields["their_sota_ref"].toString().toUpper());
    q.bindValue(":their_grid",      fields["their_grid"].toString().toUpper());
    q.bindValue(":their_name",      fields["their_name"].toString());
    q.bindValue(":their_qth",       fields["their_qth"].toString());
    q.bindValue(":their_fd",        fields["their_fd"].toString().toUpper());
    q.bindValue(":frequency_hz",    QVariant::fromValue(hz));
    q.bindValue(":band",            band);
    q.bindValue(":mode",            "DIGITAL");
    q.bindValue(":submode",         "HAVEN-FSK");
    q.bindValue(":notes",           fields["notes"].toString());
    q.bindValue(":my_callsign",     fields["my_callsign"].toString().toUpper());
    q.bindValue(":my_grid",         fields["my_grid"].toString().toUpper());
    q.bindValue(":my_pota_refs",    myParks.join(" "));
    q.bindValue(":my_sota_ref",     fields["my_sota_ref"].toString().toUpper());
    q.bindValue(":my_fd_class",     fields["my_fd_class"].toString().toUpper());
    q.bindValue(":my_fd_section",   fields["my_fd_section"].toString().toUpper());
    q.bindValue(":my_op_name",      fields["my_op_name"].toString());

    if (!q.exec()) {
        QString err = q.lastError().text();
        qWarning() << "LogManager: insert failed:" << err;
        emit logError("Failed to save contact: " + err);
        return false;
    }

    qDebug() << "LogManager: logged"
             << fields["their_callsign"].toString()
             << fields["time_utc"].toString();
    emit contactSaved(fields);
    return true;
}

QList<QVariantMap> LogManager::contactsForDate(const QString& date) const {
    QList<QVariantMap> result;
    if (!m_open) return result;

    QSqlQuery q(m_db);
    q.prepare("SELECT * FROM contacts WHERE date_utc = :date "
              "ORDER BY time_utc ASC");
    q.bindValue(":date", date);
    q.exec();

    while (q.next()) {
        QVariantMap row;
        QSqlRecord rec = q.record();
        for (int i = 0; i < rec.count(); i++)
            row[rec.fieldName(i)] = q.value(i);
        result.append(row);
    }
    return result;
}

QList<QVariantMap> LogManager::contactsForDateAndPark(
    const QString& date,
    const QString& myPotaRef) const
{
    QList<QVariantMap> result;
    if (!m_open) return result;

    // Filter in C++ — my_pota_refs is space-separated
    QSqlQuery q(m_db);
    q.prepare("SELECT * FROM contacts WHERE date_utc = :date "
              "ORDER BY time_utc ASC");
    q.bindValue(":date", date);
    q.exec();

    while (q.next()) {
        QSqlRecord rec = q.record();
        QString refs = q.value("my_pota_refs").toString();
        QStringList refList = refs.split(' ', Qt::SkipEmptyParts);
        if (refList.contains(myPotaRef.toUpper())) {
            QVariantMap row;
            for (int i = 0; i < rec.count(); i++)
                row[rec.fieldName(i)] = q.value(i);
            result.append(row);
        }
    }
    return result;
}

QList<QVariantMap> LogManager::contactsForDateAndSota(
    const QString& date,
    const QString& mySotaRef) const
{
    QList<QVariantMap> result;
    if (!m_open) return result;

    QSqlQuery q(m_db);
    q.prepare("SELECT * FROM contacts WHERE date_utc = :date "
              "AND my_sota_ref = :sota ORDER BY time_utc ASC");
    q.bindValue(":date", date);
    q.bindValue(":sota", mySotaRef.toUpper());
    q.exec();

    while (q.next()) {
        QSqlRecord rec = q.record();
        QVariantMap row;
        for (int i = 0; i < rec.count(); i++)
            row[rec.fieldName(i)] = q.value(i);
        result.append(row);
    }
    return result;
}

QList<QString> LogManager::datesWithContacts() const {
    QList<QString> dates;
    if (!m_open) return dates;

    QSqlQuery q(m_db);
    q.exec("SELECT DISTINCT date_utc FROM contacts ORDER BY date_utc DESC");
    while (q.next())
        dates.append(q.value(0).toString());
    return dates;
}

QStringList LogManager::potaRefsForDate(const QString& date) const {
    QStringList refs;
    if (!m_open) return refs;

    QSqlQuery q(m_db);
    q.prepare("SELECT DISTINCT my_pota_refs FROM contacts "
              "WHERE date_utc = :date AND my_pota_refs != ''");
    q.bindValue(":date", date);
    q.exec();

    QStringList all;
    while (q.next())
        all << q.value(0).toString().split(' ', Qt::SkipEmptyParts);

    for (const QString& ref : all)
        if (!refs.contains(ref.toUpper()))
            refs.append(ref.toUpper());
    return refs;
}

QStringList LogManager::sotaRefsForDate(const QString& date) const {
    QStringList refs;
    if (!m_open) return refs;

    QSqlQuery q(m_db);
    q.prepare("SELECT DISTINCT my_sota_ref FROM contacts "
              "WHERE date_utc = :date AND my_sota_ref != ''");
    q.bindValue(":date", date);
    q.exec();
    while (q.next())
        refs.append(q.value(0).toString().toUpper());
    return refs;
}

int LogManager::contactCountForDate(const QString& date) const {
    if (!m_open) return 0;
    QSqlQuery q(m_db);
    q.prepare("SELECT COUNT(*) FROM contacts WHERE date_utc = :date");
    q.bindValue(":date", date);
    q.exec();
    if (q.next()) return q.value(0).toInt();
    return 0;
}
