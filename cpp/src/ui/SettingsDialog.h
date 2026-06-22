#pragma once
#include <QDialog>
#include <QTabWidget>
#include <QLineEdit>
#include <QListWidget>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QGroupBox>

// SettingsDialog — Station Information and Audio device settings.
// Radio control configuration has moved to Radio → Configure... (ADR-055).

class SettingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget* parent = nullptr);

signals:
    void settingsChanged();

private slots:
    void onOk();
    void onApply();
    void onAddPotaRef();
    void onRemovePotaRef();

private:
    void setupStationTab();
    void setupAudioTab();
    void loadSettings();
    void saveSettings();

    QTabWidget* m_tabs {nullptr};

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

    QLineEdit*   m_sotaRef     {nullptr};
    QLineEdit*   m_fdClass     {nullptr};
    QLineEdit*   m_fdSection   {nullptr};

    // ── Audio Devices tab ─────────────────────────────────────────────────
    QComboBox*  m_inputDev  {nullptr};
    QComboBox*  m_outputDev {nullptr};
};
