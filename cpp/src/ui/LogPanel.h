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

class RadioInterface;

namespace HavenFSK {
    struct RxMeasurement;
}

// LogPanel — unified log entry and recent contacts display.
//
// Entry strip at top + completed contacts table below in one visual container.
// Context-adaptive: POTA/SOTA fields shown based on station info.
// Field Day mode (menu toggle) replaces activity fields with FD exchange.

class LogPanel : public QGroupBox
{
    Q_OBJECT
public:
    explicit LogPanel(QWidget* parent = nullptr);

    void setFieldDayMode(bool enabled);
    bool isFieldDayMode() const { return m_fdMode; }

    // Populate entry fields from clicked RX element
    void populateField(const QString& scheme, const QString& value);

    void setRsSent(const QString& rs);
    void setFrequency(uint64_t hz);

    // Refresh field visibility from current station info
    void refresh();

signals:
    void contactLogged(const QVariantMap& fields);
    void entryCleared();

private slots:
    void onLogIt();
    void onClear();
    void onContactRowClicked(int row, int col);

private:
    void setupEntryStrip();
    void setupContactTable();
    void updateFieldVisibility();
    void addContactRow(const QVariantMap& fields);
    QString formatFrequency(uint64_t hz) const;

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

    QLabel* m_parksLabel {nullptr};
    QLabel* m_sotaLabel  {nullptr};
    QLabel* m_gridLabel  {nullptr};
    QLabel* m_nameLabel  {nullptr};
    QLabel* m_qthLabel   {nullptr};
    QLabel* m_fdLabel    {nullptr};

    // ── Recent contacts table ─────────────────────────────────────────────
    QTableWidget* m_contactTable {nullptr};

    bool     m_fdMode    {false};
    uint64_t m_frequency {0};

    static constexpr int MAX_VISIBLE_ROWS = 10;
};
