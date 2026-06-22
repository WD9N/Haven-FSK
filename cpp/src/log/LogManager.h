#pragma once
#include <QObject>
#include <QSqlDatabase>
#include <QSqlRecord>
#include <QVariantMap>
#include <QList>
#include <QString>
#include <QDate>

// LogManager — persistent QSO log using SQLite via Qt6::Sql.
//
// Database location:
//   Windows: %APPDATA%\WD9N\HAVEN-FSK\haven_fsk_log.db
//   Linux:   ~/.local/share/WD9N/HAVEN-FSK/haven_fsk_log.db
//   macOS:   ~/Library/Application Support/WD9N/HAVEN-FSK/haven_fsk_log.db
//
// Schema: contacts table — one row per QSO, self-contained for export.
// All station info fields are snapshotted at log time per ADR-046.
//
// Thread safety: main thread only.
// Each QSO is written immediately when logContact() is called.

class LogManager : public QObject
{
    Q_OBJECT
public:
    explicit LogManager(QObject* parent = nullptr);
    ~LogManager() override;

    bool open();
    void close();
    bool isOpen() const;

    bool logContact(const QVariantMap& fields);

    // Update an existing contact by database ID (Fix 12B)
    bool updateContact(int dbId, const QVariantMap& fields);

    QList<QVariantMap> contactsForDate(const QString& date) const;
    QList<QVariantMap> contactsForDateAndPark(const QString& date,
                                               const QString& myPotaRef) const;
    QList<QVariantMap> contactsForDateAndSota(const QString& date,
                                               const QString& mySotaRef) const;

    QList<QString> datesWithContacts() const;
    QStringList    potaRefsForDate(const QString& date) const;
    QStringList    sotaRefsForDate(const QString& date) const;
    int            contactCountForDate(const QString& date) const;

    QString databasePath() const { return m_dbPath; }

signals:
    void contactSaved(const QVariantMap& fields);
    void logError(const QString& message);

private:
    bool    createSchema();
    QString buildDbPath() const;

    QSqlDatabase m_db;
    QString      m_dbPath;
    bool         m_open {false};
};
