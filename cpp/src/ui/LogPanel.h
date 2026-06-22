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
    void setFrequency(uint64_t hz);
    void refresh();

signals:
    void contactLogged(const QVariantMap& fields);
    void contactUpdated(const QVariantMap& fields);
    void contactDeleted(int dbId);
    void entryCleared();

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
    QLineEdit*   m_fdExchange  {nullptr};
    QLineEdit*   m_notes       {nullptr};
    QPushButton* m_logButton   {nullptr};
    QPushButton* m_clearButton {nullptr};
    QPushButton* m_deleteButton{nullptr};

    QLabel* m_parksLabel {nullptr};
    QLabel* m_sotaLabel  {nullptr};
    QLabel* m_gridLabel  {nullptr};
    QLabel* m_nameLabel  {nullptr};
    QLabel* m_qthLabel   {nullptr};
    QLabel* m_fdLabel    {nullptr};

    // ── Recent contacts table ─────────────────────────────────────────────
    QTableWidget* m_contactTable {nullptr};

    bool     m_fdMode      {false};
    uint64_t m_frequency   {0};
    int      m_editingRow  {-1};

    static constexpr int MAX_VISIBLE_ROWS = 10;
};
