#pragma once
#include <QMainWindow>
#include <QTextEdit>
#include <QPushButton>
#include <QLabel>
#include <QProgressBar>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QAction>
#include <QDockWidget>
#include <QToolBar>
#include <QCloseEvent>
#include <QThread>
#include <cstdint>
#include <cmath>
#include <vector>
#include "../radio/PTTManager.h"
#include "../dsp/IModem.h"

class AudioEngine;

namespace HavenFSK {
    class DspPipeline;
    struct RxMessage;
}

class SettingsDialog;
class StationInfoWidget;
class RadioInterface;
class RxDisplay;
class LogPanel;
class MacroPanel;
class LogManager;
class ExportDialog;
class WaterfallWidget;
class FrequencyControl;
class RadioConfigDialog;
class LevelPanel;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onTransmit();
    void onMessageReceived(const HavenFSK::RxMessage& msg);
    void onTxComplete();
    void onTransmitFailed(const QString& reason);
    void onTxStartError(const QString& message);
    void onAudioError(const QString& message);
    void onRxLevelChanged(float level);
    void onSettingsChanged();
    void onOpenSettings();
    void onRadioConnected();
    void onRadioDisconnected();
    void onRadioConnectFailed(const QString& reason);
    void onFrequencyChanged(uint64_t hz);
    void onWatchdogTripped();
    void onPttReleaseFailed();
    void onElementClicked(const QString& scheme, const QString& value);
    void onContactLogged(const QVariantMap& fields);
    void onFieldDayToggled(bool enabled);
    void onExport();
    void onWaterfallTune(float audioHz);
    void onOpenRadioConfig();
    void onToneSweepTx();
    void onToneSweepAudioReady(const std::vector<float>& audio);
    void onModeChanged(int index);
    void onModeReady(HavenFSK::ModemMode mode, double passbandLowHz,
                      double passbandHighHz, const QString& modeName);
    void onSquelchChanged(double value);

private:
    void setupUi();
    void setupMenu();
    void setupConnections();
    void startAudio();
    void startRadio();
    void stopRadio();

    // ── UI widgets ────────────────────────────────────────────────────────
    StationInfoWidget* m_stationInfo  {nullptr};
    WaterfallWidget*   m_waterfall    {nullptr};
    RxDisplay*         m_rxDisplay    {nullptr};
    LogPanel*          m_logPanel     {nullptr};
    MacroPanel*        m_macroPanel   {nullptr};
    QTextEdit*         m_txInput      {nullptr};

    // ── Dock widgets — movable/resizable panels (see DECISIONS.md) ────────
    // Five independently movable/resizable panels, replacing the old
    // fixed-order QSplitter. m_stationInfo and the status bar stay fixed
    // (pinned top/bottom toolbars, see m_topBar/m_bottomBar below) — not
    // part of this dockable set. Macro Panel shares dockTransmit with the
    // TX input again (back from being its own dock, per operator request).
    QDockWidget*       m_dockWaterfall {nullptr};
    QDockWidget*       m_dockReceived  {nullptr};
    QDockWidget*       m_dockLog       {nullptr};
    QDockWidget*       m_dockTransmit  {nullptr};
    QDockWidget*       m_dockLevels    {nullptr};
    QToolBar*          m_topBar        {nullptr};  // fixed, non-movable — m_stationInfo
    QToolBar*          m_bottomBar     {nullptr};  // fixed, non-movable — status row
    LevelPanel*        m_levelPanel   {nullptr};
    QPushButton*       m_txButton        {nullptr};
    QPushButton*       m_toneTestButton  {nullptr};
    QPushButton*       m_monitorButton   {nullptr};
    bool               m_toneSweepActive {false};
    // Guards onTxStartError() (connected to AudioEngine::audioError) so it
    // only reacts to a failure of the startTx() call it's paired with, not
    // an unrelated audioError (e.g. an RX format mismatch) firing at some
    // other time. Set true immediately before each startTx() call, cleared
    // on both the failure path (onTxStartError) and the success path
    // (onTxComplete).
    bool               m_awaitingTxStart {false};
    QLabel*            m_statusLabel  {nullptr};
    FrequencyControl*  m_freqControl  {nullptr};
    QLabel*            m_rigLabel     {nullptr};
    QProgressBar*      m_rxLevel      {nullptr};
    QComboBox*         m_modeCombo    {nullptr};
    QDoubleSpinBox*    m_squelchSpin  {nullptr};

    // Active modem's name (IModem::modeName() via DspPipeline::modeReady)
    // — stamped into each logged contact so mode/submode reflect the mode
    // the QSO was actually made in, not a hardcoded HAVEN-FSK.
    QString            m_currentModeName {"Haven MFSK"};

    // ── Menu actions ──────────────────────────────────────────────────────
    QAction* m_settingsAction {nullptr};
    QAction* m_exportAction   {nullptr};
    QAction* m_fdModeAction   {nullptr};
    QAction* m_recordRxAction {nullptr};

    // ── RX audio capture (debug tool) ───────────────────────────────────────
    // Records exactly the samples MfskModem/Psk31Modem receive via
    // AudioEngine::rxDataReady — not a separately-recorded file from
    // external software, which would leave open the question of whether
    // it used the same device/level/format HAVEN itself sees. Written to
    // <exe dir>/rx_capture.wav (same convention as haven_debug.log) when
    // stopped, either manually or via the size cap below.
    //
    // Stores raw float samples via one bulk insert() per chunk rather than
    // converting to int16 per-sample in real time — this tool exists to
    // observe the real-time audio path without perturbing it, so its own
    // per-chunk cost on the main thread (the same thread responsible for
    // draining the OS audio buffer in time) should be as close to zero as
    // possible. int16 conversion happens once, at save time, off the
    // real-time path entirely.
    bool               m_recordingRx {false};
    std::vector<float> m_rxRecordBuffer;
    static constexpr int RX_RECORD_MAX_SAMPLES = 48000 * 300;  // 5 min cap
    void onRecordRxToggled(bool on);
    void saveRxRecording();

    // ── Backend objects ───────────────────────────────────────────────────
    // m_audio + m_pipeline live on m_dspThread, not the GUI thread — see
    // the constructor (moveToThread right after construction) and the
    // destructor (explicit teardown; no longer parent-owned once
    // un-parented for the thread move, so no automatic cleanup). All calls
    // into them from GUI-thread code go through QMetaObject::invokeMethod
    // with Qt::AutoConnection so the same code is correct whether or not
    // the thread move has happened (see DECISIONS.md).
    QThread*               m_dspThread {nullptr};
    AudioEngine*           m_audio      {nullptr};
    HavenFSK::DspPipeline* m_pipeline  {nullptr};
    RadioInterface*        m_radio      {nullptr};
    LogManager*            m_logManager {nullptr};
    PTTManager*            m_pttManager {nullptr};
};
