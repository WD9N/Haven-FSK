#pragma once
#include <QWidget>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGroupBox>
#include <QStringList>
#include <QVariant>
#include <QDateTime>
#include <QHash>
#include <cstdint>
#include "../radio/RadioSettings.h"

namespace HavenFSK {
    struct RxMeasurement;
}

// LogPanel — unified log entry and recent contacts display.
// Entry strip at top + completed contacts table below.
// Single-click row: populate fields. Double-click: enter edit mode.

class LogPanel : public QGroupBox
{
    Q_OBJECT
public:
    explicit LogPanel(QWidget* parent = nullptr);

    void setFieldDayMode(bool enabled);
    bool isFieldDayMode() const { return m_fdMode; }

    void populateField(const QString& scheme, const QString& value);
    void setRsSent(const QString& rs);

    // RS-S from a decoded message, hands-off path (vs. setRsSent, the
    // explicit callsign-click path which always applies). Applies only
    // when the sender is the station currently in the entry and RS-S is
    // still empty — a bystander's decode, or a manual override, must not
    // be clobbered mid-QSO. Returns true if applied.
    bool maybeSetAutoRsSent(const QString& senderCallsign, const QString& rs);
    void setFrequency(uint64_t hz);
    void refresh();

    // Called on every fully-decoded RX message to reduce manual field
    // entry. Population follows the ADR-133 multi-party rule set:
    // inline field markers are authoritative where present (text tags
    // are the fallback); pair-scoped fields (RS) require the message to
    // be addressed to me; broadcast fields also fill from unaddressed
    // (CQ/info) messages; a non-empty entry holding a different
    // callsign is NEVER auto-replaced (only Log It / Clear / a click
    // switches contacts — auto-takeover was the interloper hole); a
    // station logged within the last ~10 min doesn't re-seed an empty
    // entry (their late "TU 73" must not block the next caller). Filled
    // fields are only ever empty ones — a manual edit is never
    // clobbered.
    void autoPopulateFromMessage(const QString& senderCallsign,
                                  const QString& text);

    // Repopulate the contacts table from database rows (today's contacts,
    // ascending time order) — a mid-day restart must not hide already-
    // logged contacts or orphan them from edit/delete (they'd have no
    // db_id otherwise).
    void loadContacts(const QList<QVariantMap>& contacts);

    // Connected to LogManager::contactSaved — stamps the freshly-inserted
    // database id onto the table row addContactRow() just created, so a
    // later edit or delete of this row can reference the persisted record.
    // Without this the db_id never reaches the table and updates/deletes
    // silently touch only the display, never the database.
    void onContactPersisted(const QVariantMap& fields);

signals:
    void contactLogged(const QVariantMap& fields);
    void contactUpdated(const QVariantMap& fields);
    void contactDeleted(int dbId);
    void entryCleared();

    // Fires whenever the "Their Call" entry field's displayed value
    // changes, whether from typing, populateField() (clicking a received
    // callsign), or clearing — keeps consumers (MacroPanel's <theirCall>)
    // in sync with whatever the log's callsign field currently shows,
    // not just literal keystrokes.
    void theirCallChanged(const QString& call);

    // Same contract as theirCallChanged, for the RS-S field — fires on
    // typing, auto-compute, click-populate, and clearing, so MacroPanel's
    // <rstSent> always mirrors what the log entry currently shows.
    void rsSentChanged(const QString& rs);

private slots:
    void onLogIt();
    void onClear();
    void onDeleteEntry();
    void onContactRowClicked(int row, int col);
    void onContactRowDoubleClicked(int row, int col);

private:
    void setupEntryStrip();
    void setupContactTable();
    void updateFieldVisibility();
    void addContactRow(const QVariantMap& fields);
    void updateContactRow(int row, const QVariantMap& fields);
    void exitEditMode();

    // ── Entry strip ───────────────────────────────────────────────────────
    QLineEdit*   m_callEntry   {nullptr};
    QLineEdit*   m_rsReceived  {nullptr};
    QLineEdit*   m_rsSent      {nullptr};
    QLineEdit*   m_theirParks  {nullptr};
    QLineEdit*   m_theirSota   {nullptr};
    QLineEdit*   m_theirGrid   {nullptr};
    QLineEdit*   m_theirName   {nullptr};
    QLineEdit*   m_theirQth    {nullptr};
    QLineEdit*   m_theirState  {nullptr};
    QLineEdit*   m_theirCounty {nullptr};
    QLineEdit*   m_fdExchange  {nullptr};
    QLineEdit*   m_notes       {nullptr};
    // Frequency/date/time entry — blank means "auto": live dial frequency
    // and now-UTC for new entries, the row's original values in edit mode.
    // Manual entry supports no-radio operation and paper-log transcription.
    QLineEdit*   m_freqEntry   {nullptr};
    QLineEdit*   m_dateEntry   {nullptr};
    QLineEdit*   m_timeEntry   {nullptr};
    QPushButton* m_logButton   {nullptr};
    QPushButton* m_clearButton {nullptr};
    QPushButton* m_deleteButton{nullptr};

    QLabel* m_parksLabel {nullptr};
    QLabel* m_sotaLabel  {nullptr};
    QLabel* m_gridLabel  {nullptr};
    QLabel* m_nameLabel  {nullptr};
    QLabel* m_qthLabel   {nullptr};
    QLabel* m_stateLabel  {nullptr};
    QLabel* m_countyLabel {nullptr};
    QLabel* m_fdLabel    {nullptr};

    // ── Recent contacts table ─────────────────────────────────────────────
    QTableWidget* m_contactTable {nullptr};

    bool     m_fdMode      {false};
    uint64_t m_frequency   {0};
    // Callsign → UTC time it was last logged; consulted by
    // autoPopulateFromMessage's recently-logged suppression (ADR-133
    // rule 5) and pruned opportunistically on each Log It.
    QHash<QString, QDateTime> m_recentlyLogged;
    static constexpr int RELOG_SUPPRESS_SECS = 600;
    int      m_editingRow  {-1};
    // True once the operator (or edit mode) has put a value in m_freqEntry —
    // stops setFrequency()'s live radio updates from overwriting it.
    bool     m_freqManual  {false};

    static QString mhzText(uint64_t hz);

    static constexpr int MAX_VISIBLE_ROWS = 10;
};
