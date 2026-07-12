#pragma once
#include <QDialog>
#include <QLineEdit>
#include <QListWidget>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QGroupBox>

// SettingsDialog — one settings page per window (operator request: the
// menu bar's Station Info and Audio entries each open ONLY their own
// settings). The page is fixed at construction; load/save touch only
// that page's persisted values, never the other's.
// Radio control configuration has moved to Radio → Configure... (ADR-055).

class SettingsDialog : public QDialog
{
    Q_OBJECT
public:
    enum class Page { StationInfo, Audio };

    explicit SettingsDialog(Page page, QWidget* parent = nullptr);

signals:
    void settingsChanged();

private slots:
    void onOk();
    void onApply();
    void onAddPotaRef();
    void onRemovePotaRef();

private:
    QWidget* buildStationPage();
    QWidget* buildAudioPage();
    void loadSettings();
    void saveSettings();

    Page m_page;

    // ── Station Information tab ───────────────────────────────────────────
    QLineEdit*   m_callsign    {nullptr};
    QLineEdit*   m_grid        {nullptr};
    QLineEdit*   m_opName      {nullptr};
    QLabel*      m_callWarning {nullptr};

    // POTA — dynamic unbounded list
    QListWidget* m_potaList    {nullptr};
    QLineEdit*   m_potaEntry   {nullptr};
    QPushButton* m_potaAdd     {nullptr};
    QPushButton* m_potaRemove  {nullptr};

    QLineEdit*   m_state       {nullptr};
    QLineEdit*   m_county      {nullptr};
    QLineEdit*   m_qth         {nullptr};
    QLineEdit*   m_sotaRef     {nullptr};
    QLineEdit*   m_fdClass     {nullptr};
    QLineEdit*   m_fdSection   {nullptr};

    // ── Audio Devices tab ─────────────────────────────────────────────────
    QComboBox*  m_inputDev  {nullptr};
    QComboBox*  m_outputDev {nullptr};
};
