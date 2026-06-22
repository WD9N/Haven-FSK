#pragma once
#include <QMainWindow>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QProgressBar>
#include <QAction>
#include <QSplitter>
#include <QCloseEvent>
#include <cstdint>
#include <cmath>

class AudioEngine;

namespace HavenFSK {
    class DspPipeline;
    struct RxMessage;
    enum class RxState;
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
    void onElementClicked(const QString& scheme, const QString& value);
    void onMacroTriggered(const QString& text, bool autoTx);
    void onContactLogged(const QVariantMap& fields);
    void onFieldDayToggled(bool enabled);
    void onExport();
    void onWaterfallTune(float audioHz);
    void onOpenRadioConfig();

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
    QSplitter*         m_splitter     {nullptr};
    QLineEdit*         m_txInput      {nullptr};
    QPushButton*       m_txButton     {nullptr};
    QLabel*            m_dcdLabel     {nullptr};
    QLabel*            m_statusLabel  {nullptr};
    QLabel*            m_rxStateLabel {nullptr};
    FrequencyControl*  m_freqControl  {nullptr};
    QLabel*            m_rigLabel     {nullptr};
    QProgressBar*      m_rxLevel      {nullptr};

    // ── Menu actions ──────────────────────────────────────────────────────
    QAction* m_settingsAction {nullptr};
    QAction* m_exportAction   {nullptr};
    QAction* m_fdModeAction   {nullptr};

    // ── Backend objects ───────────────────────────────────────────────────
    AudioEngine*           m_audio      {nullptr};
    HavenFSK::DspPipeline* m_pipeline  {nullptr};
    RadioInterface*        m_radio      {nullptr};
    LogManager*            m_logManager {nullptr};
};
