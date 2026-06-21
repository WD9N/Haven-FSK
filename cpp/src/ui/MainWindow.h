#pragma once
#include <QMainWindow>
#include <QTextEdit>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QProgressBar>
#include <QAction>
#include <cstdint>

class AudioEngine;

namespace HavenFSK {
    class DspPipeline;
    struct RxMessage;
    enum class RxState;
}

class SettingsDialog;
class StationInfoWidget;
class RadioInterface;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

private slots:
    void onTransmit();
    void onMessageReceived(const HavenFSK::RxMessage& msg);
    void onDcdChanged(bool active);
    void onRxStateChanged(HavenFSK::RxState state);
    void onTxComplete();
    void onAudioError(const QString& message);
    void onRxLevelChanged(float level);
    void onSettingsChanged();
    void onOpenSettings();
    void onRadioConnected();
    void onRadioDisconnected();
    void onFrequencyChanged(uint64_t hz);
    void onWatchdogTripped();
    void onChannelBusy();

private:
    void setupUi();
    void setupMenu();
    void setupConnections();
    void startAudio();
    void startRadio();
    void stopRadio();
    QString formatFrequency(uint64_t hz) const;

    // ── UI widgets ────────────────────────────────────────────────────────
    StationInfoWidget* m_stationInfo  {nullptr};
    QTextEdit*         m_rxText       {nullptr};
    QLineEdit*         m_txInput      {nullptr};
    QPushButton*       m_txButton     {nullptr};
    QLabel*            m_dcdLabel     {nullptr};
    QLabel*            m_statusLabel  {nullptr};
    QLabel*            m_rxStateLabel {nullptr};
    QLabel*            m_freqLabel    {nullptr};
    QLabel*            m_rigLabel     {nullptr};
    QProgressBar*      m_rxLevel      {nullptr};

    // ── Menu actions ──────────────────────────────────────────────────────
    QAction* m_settingsAction      {nullptr};
    QAction* m_connectRigAction    {nullptr};
    QAction* m_disconnectRigAction {nullptr};

    // ── Backend objects ───────────────────────────────────────────────────
    AudioEngine*           m_audio    {nullptr};
    HavenFSK::DspPipeline* m_pipeline {nullptr};
    RadioInterface*        m_radio    {nullptr};
};
