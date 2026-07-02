#include "MainWindow.h"
#include "SettingsDialog.h"
#include "StationInfoWidget.h"
#include "RxDisplay.h"
#include "LogPanel.h"
#include "MacroPanel.h"
#include "ExportDialog.h"
#include "RadioConfigDialog.h"
#include "WaterfallWidget.h"
#include "FrequencyControl.h"
#include "LevelPanel.h"
#include <QTextEdit>
#include <QShortcut>
#include "../log/LogManager.h"
#include "../audio/AudioEngine.h"
#include "../audio/AudioSettings.h"
#include "../radio/RadioSettings.h"
#include "../radio/RadioInterface.h"
#include "../radio/RigctldClient.h"
#include "../radio/TCIClient.h"
#include "../dsp/DspPipeline.h"
#include "../dsp/Constants.h"
// TODO(Phase 5): BASE_FREQ usages below are MFSK-specific tuning-offset
// math; should become mode-generic once IModem passband is UI-wired.
#include "../dsp/MfskConstants.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QMenuBar>
#include <QMenu>
#include <QMessageBox>
#include <QDateTime>
#include <QTimer>
#include <QFont>
#include <QSettings>
#include <QVariantMap>
#include <QDebug>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(
        QString("%1 v%2")
        .arg(HavenFSK::APP_NAME)
        .arg(HavenFSK::APP_VERSION));
    setMinimumSize(800, 820);

    m_audio      = new AudioEngine(this);
    m_pipeline   = new HavenFSK::DspPipeline(this);
    m_logManager = new LogManager(this);
    if (!m_logManager->open()) {
        QMessageBox::warning(this, "Log Database",
            "Could not open log database.\n"
            "Contacts will not be saved this session.\n\n"
            "Database path: " + m_logManager->databasePath());
    }

    // PTTManager starts with no radio — updated when radio connects
    m_pttManager = new PTTManager(nullptr, this);
    connect(m_pttManager, &PTTManager::watchdogTripped,
            this, &MainWindow::onWatchdogTripped);

    setupMenu();
    setupUi();
    setupConnections();

    // Restore last-used mode; setting the combo index triggers
    // onModeChanged() -> DspPipeline::setMode(), so this both applies the
    // saved mode and updates the waterfall passband/labels for it.
    {
        QSettings s;
        int savedMode = s.value("mode/current",
            static_cast<int>(HavenFSK::ModemMode::Mfsk16)).toInt();
        int idx = m_modeCombo->findData(savedMode);
        m_modeCombo->setCurrentIndex(idx >= 0 ? idx : 0);
        // setCurrentIndex() is a no-op (no signal fired) when the target
        // index equals the combo's default (0) — call explicitly so the
        // waterfall/labels are always initialized on first run.
        onModeChanged(m_modeCombo->currentIndex());
    }

    m_stationInfo->refresh();
    onSettingsChanged();

    // Fix 2: warn if saved output device is unavailable
    {
        QString savedOut = HavenFSK::savedOutputDevice();
        QStringList outputs = AudioEngine::availableOutputDevices();
        if (savedOut.isEmpty()) {
            qWarning() << "No audio output device configured";
        } else if (!outputs.contains(savedOut)) {
            qWarning() << "Saved output device not found:" << savedOut;
            m_statusLabel->setText(
                "⚠ Audio output device not found — check Settings → Audio");
        }
    }

    startAudio();
    startRadio();
}

MainWindow::~MainWindow() {
    stopRadio();
}

void MainWindow::closeEvent(QCloseEvent* event) {
    QSettings s;
    s.setValue("ui/splitterState4", m_splitter->saveState());
    QMainWindow::closeEvent(event);
}

void MainWindow::setupMenu() {
    // File menu
    QMenu* fileMenu  = menuBar()->addMenu("&File");
    m_settingsAction = new QAction("&Settings...", this);
    m_settingsAction->setShortcut(Qt::CTRL | Qt::Key_Comma);
    fileMenu->addAction(m_settingsAction);
    m_exportAction = new QAction("&Export Log...", this);
    m_exportAction->setShortcut(Qt::CTRL | Qt::Key_E);
    fileMenu->addAction(m_exportAction);

    // Fix 12E: View Log menu item
    auto* viewLogAction = new QAction("&View Log...", this);
    viewLogAction->setShortcut(Qt::CTRL | Qt::Key_L);
    connect(viewLogAction, &QAction::triggered, this, [this]() {
        if (m_logManager && m_logManager->isOpen()) {
            QString today = QDateTime::currentDateTimeUtc()
                                .toString("yyyyMMdd");
            auto contacts = m_logManager->contactsForDate(today);
            QMessageBox::information(this, "Log",
                QString("%1 contacts logged today (UTC).\n"
                        "Use File → Export Log to export.")
                .arg(contacts.size()));
        }
    });
    fileMenu->addAction(viewLogAction);

    fileMenu->addSeparator();
    auto* quitAction = new QAction("&Quit", this);
    quitAction->setShortcut(Qt::CTRL | Qt::Key_Q);
    connect(quitAction, &QAction::triggered, this, &QWidget::close);
    fileMenu->addAction(quitAction);

    // Radio — single click opens config dialog directly (RADIO_FIX)
    auto* radioAction = new QAction("Radio", this);
    menuBar()->addAction(radioAction);
    connect(radioAction, &QAction::triggered,
            this, &MainWindow::onOpenRadioConfig);

    // Operating menu
    QMenu* opMenu  = menuBar()->addMenu("&Operating");
    m_fdModeAction = new QAction("&Field Day Mode", this);
    m_fdModeAction->setCheckable(true);
    m_fdModeAction->setChecked(false);
    opMenu->addAction(m_fdModeAction);
    connect(m_fdModeAction, &QAction::toggled,
            this, &MainWindow::onFieldDayToggled);

    opMenu->addSeparator();
    auto* afcAction = new QAction("AFC — Auto Frequency Correct", this);
    afcAction->setCheckable(true);
    afcAction->setChecked(true);
    opMenu->addAction(afcAction);
    connect(afcAction, &QAction::toggled, this, [this](bool on) {
        m_pipeline->setAfcEnabled(on);
        if (!on)
            m_waterfall->setAfcOffset(0.0f);
    });

    // Help menu
    QMenu* helpMenu   = menuBar()->addMenu("&Help");
    auto* aboutAction = new QAction("&About HAVEN-FSK", this);
    connect(aboutAction, &QAction::triggered, this, [this]() {
        QMessageBox::about(this,
            "About HAVEN-FSK",
            QString("<b>HAVEN-FSK v%1</b><br>"
                    "HF Digital Mode application — MFSK-16 and PSK31<br><br>"
                    "Copyright 2026 WD9N<br>"
                    "Licensed under GPL-3.0<br><br>"
                    "<a href='%2'>%2</a><br><br>"
                    "Third-party licenses: see THIRD_PARTY_LICENSES.md")
            .arg(HavenFSK::APP_VERSION)
            .arg(HavenFSK::SPEC_URL));
    });
    helpMenu->addAction(aboutAction);
}

void MainWindow::setupUi() {
    auto* central    = new QWidget(this);
    setCentralWidget(central);
    auto* mainLayout = new QVBoxLayout(central);
    mainLayout->setSpacing(4);
    mainLayout->setContentsMargins(4, 4, 4, 4);

    // Station info — fixed, always visible
    m_stationInfo = new StationInfoWidget(central);
    mainLayout->addWidget(m_stationInfo);

    // Splitter — waterfall / RX display / log panel
    m_splitter = new QSplitter(Qt::Vertical, central);

    // Waterfall — receives raw audio directly from AudioEngine
    m_waterfall = new WaterfallWidget(this);
    m_waterfall->setMinimumHeight(80);
    m_splitter->addWidget(m_waterfall);

    // RX display
    auto* rxGroup  = new QGroupBox("Received");
    auto* rxLayout = new QVBoxLayout(rxGroup);
    m_rxDisplay = new RxDisplay(rxGroup);
    m_rxDisplay->setMinimumHeight(100);
    rxLayout->addWidget(m_rxDisplay);
    m_splitter->addWidget(rxGroup);

    // Log panel
    m_logPanel = new LogPanel;
    m_logPanel->setMinimumHeight(120);
    m_splitter->addWidget(m_logPanel);

    // ── Bottom resizable container — added as 4th splitter widget ───────────
    // LevelPanel (fixed) | MacroPanel 6×3 grid + TX textarea (expanding)
    auto* bottomContainer = new QWidget(central);
    bottomContainer->setSizePolicy(
        QSizePolicy::Expanding, QSizePolicy::Expanding);

    auto* bottomHLayout = new QHBoxLayout(bottomContainer);
    bottomHLayout->setContentsMargins(0, 0, 0, 0);
    bottomHLayout->setSpacing(0);

    // Left: LevelPanel — fixed size, unchanged
    m_levelPanel = new LevelPanel(bottomContainer);
    bottomHLayout->addWidget(m_levelPanel, 0);

    // Right: macro grid above TX textarea
    auto* rightSection = new QWidget(bottomContainer);
    rightSection->setSizePolicy(
        QSizePolicy::Expanding, QSizePolicy::Expanding);

    auto* rightLayout = new QVBoxLayout(rightSection);
    rightLayout->setContentsMargins(4, 4, 4, 4);
    rightLayout->setSpacing(4);

    m_macroPanel = new MacroPanel(rightSection);
    m_macroPanel->setSizePolicy(
        QSizePolicy::Expanding, QSizePolicy::Preferred);
    rightLayout->addWidget(m_macroPanel, 0);

    // TX textarea section
    auto* txContainer = new QWidget(rightSection);
    txContainer->setSizePolicy(
        QSizePolicy::Expanding, QSizePolicy::Expanding);

    auto* txLayout = new QVBoxLayout(txContainer);
    txLayout->setContentsMargins(0, 0, 0, 0);
    txLayout->setSpacing(4);

    auto* txTopRow = new QHBoxLayout;
    auto* txLabel  = new QLabel("Transmit", txContainer);
    txLabel->setStyleSheet(
        "font-family:'Courier New'; font-size:9px;"
        "color:#666; letter-spacing:1px;");
    auto* txHint = new QLabel("Ctrl+Enter to send", txContainer);
    txHint->setStyleSheet("font-size:8px; color:#444;");
    txTopRow->addWidget(txLabel);
    txTopRow->addStretch();
    txTopRow->addWidget(txHint);
    txLayout->addLayout(txTopRow);

    m_txInput = new QTextEdit(txContainer);
    m_txInput->setPlaceholderText(
        "Type message — Enter for new line, Ctrl+Enter to send...");
    m_txInput->setFont(QFont("Courier New", 10));
    m_txInput->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    m_txInput->setAcceptRichText(false);
    m_txInput->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_txInput->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_txInput->setStyleSheet(
        "QTextEdit {"
        "  background: #141414; color: #bbb;"
        "  border: 1px solid #2a2a2a; border-radius: 3px;"
        "  padding: 4px 6px;"
        "}");
    txLayout->addWidget(m_txInput, 1);

    auto* txBtnRow = new QHBoxLayout;
    txBtnRow->addStretch();

    m_toneTestButton = new QPushButton("Tone TX", txContainer);
    m_toneTestButton->setMinimumWidth(80);
    m_toneTestButton->setToolTip(
        "Transmit all 16 HAVEN tones (500 ms each) across the RF path\n"
        "and log what the receiver hears, symbol by symbol.\n"
        "Watch the debug output for ToneMon[] lines while TX plays.\n"
        "RX pipeline stays active — use with two radios on dummy loads.");
    m_toneTestButton->setStyleSheet(
        "QPushButton {"
        "  background: #1a1a2e; color: #7a9ab0;"
        "  border: 1px solid #2a3a4a; border-radius: 3px;"
        "  padding: 4px 12px;"
        "}"
        "QPushButton:hover { background: #222240; }");
    txBtnRow->addWidget(m_toneTestButton);
    txBtnRow->addSpacing(4);

    m_monitorButton = new QPushButton("Monitor", txContainer);
    m_monitorButton->setMinimumWidth(72);
    m_monitorButton->setCheckable(true);
    m_monitorButton->setToolTip(
        "Toggle per-symbol tone logging.\n"
        "When ON, every received symbol logs one line:\n"
        "  ToneMon[N]: toneT (FFF.FF Hz)  frac=0.870\n"
        "Turn on while 590SG is transmitting tones to see\n"
        "exactly what the HermesLite demodulator hears.");
    m_monitorButton->setStyleSheet(
        "QPushButton {"
        "  background: #1a1a2e; color: #7a9ab0;"
        "  border: 1px solid #2a3a4a; border-radius: 3px;"
        "  padding: 4px 10px;"
        "}"
        "QPushButton:hover   { background: #222240; }"
        "QPushButton:checked { background: #0d2d0d; color: #7ab07a;"
        "                      border-color: #2a4a2a; }");
    txBtnRow->addWidget(m_monitorButton);
    txBtnRow->addSpacing(8);

    m_txButton = new QPushButton("Transmit", txContainer);
    m_txButton->setMinimumWidth(90);
    m_txButton->setStyleSheet(
        "QPushButton {"
        "  background: #1a2e1a; color: #7ab07a;"
        "  border: 1px solid #2a4a2a; border-radius: 3px;"
        "  padding: 4px 16px;"
        "}"
        "QPushButton:hover { background: #223322; }"
        "QPushButton:disabled { color: #444; border-color: #2a2a2a; }");
    txBtnRow->addWidget(m_txButton);
    txLayout->addLayout(txBtnRow);

    rightLayout->addWidget(txContainer, 1);
    bottomHLayout->addWidget(rightSection, 1);

    // Add all four widgets to the vertical splitter
    m_splitter->addWidget(bottomContainer);

    m_splitter->setStretchFactor(0, 2);  // waterfall
    m_splitter->setStretchFactor(1, 3);  // RX display
    m_splitter->setStretchFactor(2, 2);  // log panel
    m_splitter->setStretchFactor(3, 2);  // bottom container

    mainLayout->addWidget(m_splitter, 1);

    // Status bar — fixed
    auto* statusBar    = new QWidget(central);
    auto* statusLayout = new QHBoxLayout(statusBar);
    statusLayout->setContentsMargins(0, 0, 0, 0);
    statusLayout->setSpacing(8);

    m_freqControl = new FrequencyControl(statusBar);
    statusLayout->addWidget(m_freqControl);

    m_modeCombo = new QComboBox(statusBar);
    m_modeCombo->addItem("MFSK-16", static_cast<int>(HavenFSK::ModemMode::Mfsk16));
    m_modeCombo->addItem("PSK31",   static_cast<int>(HavenFSK::ModemMode::Psk31));
    m_modeCombo->setToolTip("Operating mode");
    statusLayout->addWidget(m_modeCombo);

    m_rigLabel = new QLabel("No rig");
    m_rigLabel->setStyleSheet("color: gray;");
    statusLayout->addWidget(m_rigLabel);

    statusLayout->addWidget(new QLabel("RX:"));
    m_rxLevel = new QProgressBar(statusBar);
    m_rxLevel->setRange(0, 100);
    m_rxLevel->setValue(0);
    m_rxLevel->setMaximumWidth(120);
    m_rxLevel->setTextVisible(false);
    statusLayout->addWidget(m_rxLevel);

    statusLayout->addStretch();

    m_statusLabel = new QLabel("Ready");
    statusLayout->addWidget(m_statusLabel);

    mainLayout->addWidget(statusBar);

    // Restore splitter state (key v4 matches the 4-widget layout)
    QSettings s;
    QByteArray splitterState = s.value("ui/splitterState4").toByteArray();
    if (!splitterState.isEmpty()) {
        m_splitter->restoreState(splitterState);
    } else {
        // First run with this layout: set explicit initial sizes so the
        // bottom container (constrained by LevelPanel ~340px fixed height)
        // gets enough room rather than being crushed by stretch-factor math.
        m_splitter->setSizes({120, 180, 140, 380});
    }
}

void MainWindow::setupConnections() {
    // Settings and export
    connect(m_settingsAction, &QAction::triggered,
            this, &MainWindow::onOpenSettings);
    connect(m_exportAction, &QAction::triggered,
            this, &MainWindow::onExport);

    // TX
    connect(m_txButton, &QPushButton::clicked,
            this, &MainWindow::onTransmit);
    connect(m_toneTestButton, &QPushButton::clicked,
            this, &MainWindow::onToneSweepTx);
    connect(m_monitorButton, &QPushButton::toggled,
            this, [this](bool on) {
                m_pipeline->setToneMonitor(on);
                m_monitorButton->setText(on ? "Monitor ON" : "Monitor");
                if (on)
                    m_statusLabel->setText(
                        "Tone monitor active — transmit tones from 590SG, "
                        "watch debug log for ToneMon[] lines");
                else
                    m_statusLabel->setText("Tone monitor off");
            });
    // QTextEdit: Ctrl+Enter transmits, plain Enter adds newline
    auto* txShortcut = new QShortcut(
        QKeySequence(Qt::CTRL | Qt::Key_Return), m_txInput);
    connect(txShortcut, &QShortcut::activated,
            this, &MainWindow::onTransmit);

    // AudioEngine → DspPipeline (AFC-corrected path)
    connect(m_audio, &AudioEngine::rxDataReady,
            m_pipeline, &HavenFSK::DspPipeline::onAudioChunk);
    connect(m_audio, &AudioEngine::txComplete,
            this, &MainWindow::onTxComplete);
    // Pipeline clears its transmitting flag when audio finishes.
    // This fires before the tail delay — m_pipeline is ready for
    // the next transmit while we wait for PTT to release.
    connect(m_audio, &AudioEngine::txComplete,
            m_pipeline, &HavenFSK::DspPipeline::onTxComplete);
    connect(m_audio, &AudioEngine::audioError,
            this, &MainWindow::onAudioError);
    connect(m_audio, &AudioEngine::rxLevelChanged,
            this, &MainWindow::onRxLevelChanged);

    // ── Waterfall receives RAW audio directly from AudioEngine ────────────
    // CRITICAL: do NOT route through DspPipeline — waterfall must see
    // unmodified audio so the display shows true signal positions.
    connect(m_audio, &AudioEngine::rxDataReady,
            m_waterfall, &WaterfallWidget::pushChunk,
            Qt::QueuedConnection);

    // Waterfall tune request
    connect(m_waterfall, &WaterfallWidget::tuneRequested,
            this, &MainWindow::onWaterfallTune);

    // Waterfall tuning line movement → status bar preview
    connect(m_modeCombo, &QComboBox::currentIndexChanged,
            this, &MainWindow::onModeChanged);

    connect(m_waterfall, &WaterfallWidget::tuningLineAt,
            this, [this](float hz) {
                if (!m_radio || !m_radio->isConnected()) return;
                uint64_t dial = m_radio->getFrequency();
                if (dial == 0) return;
                int64_t  offset = static_cast<int64_t>(hz)
                                - static_cast<int64_t>(HavenFSK::BASE_FREQ);
                uint64_t newHz  = static_cast<uint64_t>(
                    static_cast<int64_t>(dial) + offset);
                m_statusLabel->setText(
                    QString("Tune to: %1 MHz  "
                            "[Left-click confirms | Esc cancels]")
                    .arg(static_cast<double>(newHz) / 1.0e6, 0, 'f', 6));
            });

    // AFC offset → waterfall AFC marker lines
    connect(m_pipeline,
            &HavenFSK::DspPipeline::afcOffsetChanged,
            m_waterfall,
            &WaterfallWidget::setAfcOffset);

    // AFC limit warning → status bar
    connect(m_pipeline,
            &HavenFSK::DspPipeline::afcOffsetChanged,
            this, [this](float hz) {
                if (std::abs(hz) >=
                    HavenFSK::DspPipeline::AFC_MAX_HZ - 1.0f) {
                    m_statusLabel->setText(
                        "AFC limit ⚠ — please retune closer to signal");
                }
            });

    // FrequencyControl — update display and log panel; send to radio if connected
    connect(m_freqControl, &FrequencyControl::frequencyRequested,
            this, [this](uint64_t hz) {
                m_freqControl->setFrequency(hz);
                if (m_logPanel) m_logPanel->setFrequency(hz);
                if (m_radio && m_radio->isConnected()) {
                    qDebug() << "MainWindow: sending frequency" << hz << "to radio";
                    m_radio->setFrequency(hz);
                } else {
                    qDebug() << "MainWindow: no radio connected for frequency set";
                }
            });

    // DspPipeline → AudioEngine (TX audio) — PTT lead then audio
    connect(m_pipeline, &HavenFSK::DspPipeline::txAudioReady,
            this, [this](const std::vector<float>& samples) {
                QString outDev = HavenFSK::savedOutputDevice();
                int     leadMs = HavenFSK::pttLeadMs();

                // Compute initial gain from TX fader and show expected level
                float initialGain = 1.0f;
                if (m_levelPanel) {
                    float dBFS  = m_levelPanel->txFaderDbFS();
                    initialGain = std::max(0.0f,
                                  std::min(1.0f,
                                  std::pow(10.0f, dBFS / 20.0f)));
                    qDebug() << "TX: initial gain" << initialGain
                             << "(" << dBFS << "dBFS)";
                    m_levelPanel->setTxLevel(dBFS);
                }

                if (m_pttManager && m_radio && m_radio->isConnected()) {
                    bool pttOk = m_pttManager->requestTX();
                    if (!pttOk) {
                        qWarning() << "TX: PTT request failed — aborting";
                        m_pipeline->onTxComplete();
                        onTxComplete();
                        return;
                    }
                    qDebug() << "TX: PTT asserted, waiting"
                             << leadMs << "ms lead time";
                    QTimer::singleShot(leadMs, this,
                        [this, samples, outDev, initialGain]() {
                            qDebug() << "TX: lead time elapsed, starting audio";
                            bool ok = m_audio->startTx(
                                outDev, samples, initialGain);
                            if (!ok) {
                                qWarning() << "startTx failed — aborting TX";
                                m_pipeline->onTxComplete();
                                onTxComplete();
                                if (m_pttManager) m_pttManager->txOff();
                            }
                        });
                } else {
                    // No rig control — VOX mode
                    qDebug() << "TX: no rig control — VOX mode";
                    m_audio->startTx(outDev, samples, initialGain);
                }
            });

    // DspPipeline → UI
    connect(m_pipeline, &HavenFSK::DspPipeline::messageReceived,
            this, &MainWindow::onMessageReceived);

    connect(m_pipeline, &HavenFSK::DspPipeline::preambleDetected,
            this, [this](float score) {
                m_statusLabel->setText(
                    QString("Preamble found (score %1) — collecting frame...")
                    .arg(score, 0, 'f', 2));
            });

    connect(m_pipeline, &HavenFSK::DspPipeline::rxStateChanged,
            this, [this](HavenFSK::RxState state) {
                if (state == HavenFSK::RxState::Idle)
                    m_statusLabel->setText("Listening...");
            });

    // RxDisplay → MainWindow (element clicks)
    connect(m_rxDisplay, &RxDisplay::elementClicked,
            this, &MainWindow::onElementClicked);

    // MacroPanel → TX input
    connect(m_macroPanel, &MacroPanel::macroTriggered,
            this, [this](const QString& text,
                         bool clearFirst, bool autoTx) {
                if (clearFirst) {
                    m_txInput->setPlainText(text);
                } else {
                    QTextCursor cursor = m_txInput->textCursor();
                    cursor.movePosition(QTextCursor::End);
                    m_txInput->setTextCursor(cursor);
                    m_txInput->insertPlainText(text);
                }
                QTextCursor cursor = m_txInput->textCursor();
                cursor.movePosition(QTextCursor::End);
                m_txInput->setTextCursor(cursor);
                m_txInput->setFocus();
                if (autoTx)
                    QTimer::singleShot(50, this, &MainWindow::onTransmit);
            });

    // TX message → RxDisplay (amber [TX] styling)
    connect(m_pipeline,
            &HavenFSK::DspPipeline::messageTransmitted,
            this, [this](const QString& text) {
                HavenFSK::StationInfo info = HavenFSK::loadStationInfo();
                m_rxDisplay->appendTxMessage(text, info.callsign);
            });

    // RX level meter — peak of each chunk after RX gain, matching demodulator input
    connect(m_audio, &AudioEngine::rxDataReady,
            this, [this](const std::vector<float>& samples) {
                if (samples.empty() || !m_levelPanel) return;
                float peak = 0.0f;
                for (float s : samples) peak = std::max(peak, std::abs(s));
                float gain = m_pipeline ? m_pipeline->rxGain() : 1.0f;
                peak = std::min(1.0f, peak * gain);
                float dbFS = (peak > 1e-10f)
                    ? 20.0f * std::log10(peak) : -96.0f;
                m_levelPanel->setRxLevel(dbFS);
            });

    // RX fader → DspPipeline gain (0 dBu = unity gain)
    connect(m_levelPanel, &LevelPanel::rxFaderChanged,
            this, [this](float dBu) {
                float gain = std::pow(10.0f, dBu / 20.0f);
                m_pipeline->setRxGain(gain);
            });

    // TX fader → GainedAudioDevice real-time gain (always, not only while TX)
    connect(m_levelPanel, &LevelPanel::txFaderChanged,
            this, [this](float dBu) {
                float dBFS = dBu - 6.0f;
                float gain = std::max(0.0f,
                             std::min(1.0f,
                             std::pow(10.0f, dBFS / 20.0f)));
                qDebug() << "TX fader: gain" << gain << "dBu=" << dBu;
                m_audio->setTxGain(gain);
                if (m_levelPanel && m_audio->isTransmitting())
                    m_levelPanel->setTxLevel(dBFS);
            });

    // LogPanel → session log
    connect(m_logPanel, &LogPanel::contactLogged,
            this, &MainWindow::onContactLogged);

    // Delete contact → database
    connect(m_logPanel, &LogPanel::contactDeleted,
            this, [this](int dbId) {
                if (m_logManager && m_logManager->isOpen()) {
                    m_logManager->deleteContact(dbId);
                    m_statusLabel->setText("Contact deleted");
                }
            });

    // Fix 12D: log panel edit → database update
    connect(m_logPanel, &LogPanel::contactUpdated,
            this, [this](const QVariantMap& fields) {
                if (m_logManager && m_logManager->isOpen()) {
                    int dbId = fields["db_id"].toInt();
                    if (dbId > 0)
                        m_logManager->updateContact(dbId, fields);
                }
            });
}

void MainWindow::startAudio() {
    QString inDev = HavenFSK::savedInputDevice();
    if (!m_audio->startRx(inDev))
        m_statusLabel->setText(
            "Audio input error — check Settings → Audio");
    else
        m_statusLabel->setText("Listening...");
}

void MainWindow::startRadio() {
    stopRadio();

    QSettings s;
    QString method = s.value(
        HavenFSK::RadioSettingsKeys::RIG_METHOD, "none").toString();

    if (method == "rigctld") {
        m_radio = new RigctldClient(
            HavenFSK::rigctldHost(),
            HavenFSK::rigctldPort(), this);
    } else if (method == "tci") {
        m_radio = new TCIClient(
            HavenFSK::tciHost(),
            HavenFSK::tciPort(), this);
    } else {
        m_rigLabel->setText("No rig control");
        m_rigLabel->setStyleSheet("color: gray;");
        return;
    }

    connect(m_radio, &RadioInterface::connected,
            this, [this]() {
                onRadioConnected();
                if (HavenFSK::setModeOnConnect()) {
                    QString mode = HavenFSK::connectModeString();
                    if (!mode.isEmpty()) m_radio->setMode(mode);
                }
                // Recreate PTTManager now that we have a connected radio
                if (m_pttManager) m_pttManager->deleteLater();
                m_pttManager = new PTTManager(m_radio, this);
                connect(m_pttManager, &PTTManager::txStarted, this, [this]() {
                    qDebug() << "PTTManager: TX started (PTT keyed)";
                    m_statusLabel->setText("Transmitting...");
                });
                connect(m_pttManager, &PTTManager::watchdogTripped,
                        this, &MainWindow::onWatchdogTripped);
            });
    connect(m_radio, &RadioInterface::disconnected,
            this, &MainWindow::onRadioDisconnected);
    connect(m_radio, &RadioInterface::frequencyChanged,
            this, &MainWindow::onFrequencyChanged);
    connect(m_radio, &RadioInterface::frequencyChanged,
            m_logPanel, &LogPanel::setFrequency);
    connect(m_radio, &RadioInterface::rigError,
            this, [this](const QString& msg) {
                m_statusLabel->setText("Rig: " + msg);
            });

    m_radio->connect();
}

void MainWindow::stopRadio() {
    if (m_radio) {
        m_radio->disconnect();
        m_radio->deleteLater();
        m_radio = nullptr;
    }
    m_rigLabel->setText("No rig control");
    m_rigLabel->setStyleSheet("color: gray;");
    m_freqControl->setFrequency(0);
}

// ── Slots ─────────────────────────────────────────────────────────────────

void MainWindow::onOpenSettings() {
    SettingsDialog dlg(this);
    connect(&dlg, &SettingsDialog::settingsChanged,
            this, &MainWindow::onSettingsChanged);
    dlg.exec();
}

void MainWindow::onSettingsChanged() {
    m_stationInfo->refresh();
    m_logPanel->refresh();

    HavenFSK::StationInfo info = HavenFSK::loadStationInfo();
    bool hasCall = !info.callsign.isEmpty();
    m_txButton->setEnabled(hasCall);
    m_txInput->setEnabled(hasCall);
    if (!hasCall)
        m_statusLabel->setText(
            "⚠  Enter callsign in Settings before transmitting");
    else
        m_statusLabel->setText("Listening...");

    // Restart audio only — radio reconnects only from RadioConfigDialog
    // (onSettingsChanged has no radio tab; calling startRadio() here
    // would tear down and recreate TCI on every settings save)
    m_audio->stop();
    startAudio();
}

void MainWindow::onTransmit() {
    qDebug() << "=== TX START ===";
    qDebug() << "Audio transmitting:" << m_audio->isTransmitting();
    qDebug() << "Radio connected:" << (m_radio && m_radio->isConnected());
    qDebug() << "Output device:" << HavenFSK::savedOutputDevice();

    HavenFSK::StationInfo info = HavenFSK::loadStationInfo();
    if (info.callsign.isEmpty()) {
        QMessageBox::warning(this, "No Callsign",
            "Please enter your callsign in Settings → Station Info\n"
            "before transmitting.\n\n"
            "Transmitting without identifying by callsign\n"
            "violates FCC Part 97.119.");
        return;
    }

    QString text = m_txInput->toPlainText().trimmed();
    if (text.isEmpty()) return;

    if (m_audio->isTransmitting()) {
        m_statusLabel->setText("TX in progress — please wait");
        return;
    }

    m_audio->stopRx();
    m_txButton->setEnabled(false);
    m_txInput->setEnabled(false);
    m_statusLabel->setText("Transmitting...");

    if (!m_pipeline->transmit(text)) {
        m_statusLabel->setText("TX failed");
        m_txButton->setEnabled(true);
        m_txInput->setEnabled(true);
        m_audio->startRx(HavenFSK::savedInputDevice());
    }
}

void MainWindow::onTxComplete() {
    // Tone sweep TX completes here too.  Don't restart audio (RX was never
    // stopped) — just stop the monitor, release PTT, and re-enable the button.
    if (m_toneSweepActive) {
        m_toneSweepActive = false;
        m_monitorButton->setChecked(false);   // also calls setToneMonitor(false) via toggled signal
        int tailMs = HavenFSK::txTailMs();
        QTimer::singleShot(tailMs, this, [this]() {
            if (m_pttManager) m_pttManager->txOff();
            m_statusLabel->setText(
                "Tone sweep complete — check debug log for ToneMon[] lines");
        });
        m_toneTestButton->setEnabled(true);
        return;
    }

    int tailMs = HavenFSK::txTailMs();
    qDebug() << "TX: audio complete — starting" << tailMs << "ms tail timer";

    if (m_levelPanel)
        m_levelPanel->setTxLevel(-96.0f);

    // Re-enable UI immediately so operator can prepare next message
    m_txButton->setEnabled(true);
    m_txInput->setEnabled(true);
    m_txInput->clear();
    m_statusLabel->setText("TX tail — releasing PTT...");

    // Wait tail time, then release PTT and restart RX.
    // m_pipeline->onTxComplete() is called via the direct signal connection
    // in setupConnections() — do NOT call it here or it fires twice.
    QTimer::singleShot(tailMs, this, [this]() {
        qDebug() << "TX: tail complete — releasing PTT";
        if (m_pttManager) m_pttManager->txOff();
        m_statusLabel->setText("Listening...");
        m_audio->startRx(HavenFSK::savedInputDevice());
    });
}

void MainWindow::onMessageReceived(const HavenFSK::RxMessage& msg) {
    m_rxDisplay->appendMessage(
        msg.text,
        msg.senderCallsign,
        QDateTime::currentDateTimeUtc(),
        msg.crcOk,
        msg.converged);

    m_statusLabel->setText(
        QString("RX — CRC: %1  FEC: %2")
        .arg(msg.crcOk ? "OK" : "FAIL")
        .arg(msg.converged ? "OK" : "NC"));
}

void MainWindow::onAudioError(const QString& message) {
    m_statusLabel->setText("Audio: " + message);
    QMessageBox::warning(this, "Audio Error", message);
}

void MainWindow::onRxLevelChanged(float level) {
    m_rxLevel->setValue(static_cast<int>(level * 100.0f));
}

void MainWindow::onRadioConnected() {
    QString name = m_radio ? m_radio->rigName() : "Rig";
    m_rigLabel->setText(name + "  ✓");
    m_rigLabel->setStyleSheet("color: green;");

    // Request current frequency after a short settling delay.
    // Ensures FrequencyControl shows the rig's actual frequency
    // immediately on connection without waiting for a poll or tune.
    QTimer::singleShot(200, this, [this]() {
        if (m_radio && m_radio->isConnected())
            m_radio->requestFrequency();
    });
}

void MainWindow::onRadioDisconnected() {
    m_rigLabel->setText("Rig disconnected");
    m_rigLabel->setStyleSheet("color: orange;");
    m_freqControl->setFrequency(0);
}

void MainWindow::onFrequencyChanged(uint64_t hz) {
    m_freqControl->setFrequency(hz);
}

void MainWindow::onWatchdogTripped() {
    QMessageBox::warning(this, "TX Watchdog Tripped",
        "Transmission exceeded 120 seconds and was stopped.\n"
        "Check your audio and radio configuration.");
    onTxComplete();
}


void MainWindow::onElementClicked(const QString& scheme, const QString& value) {
    m_logPanel->populateField(scheme, value);

    if (scheme == "callsign") {
        m_macroPanel->setTheirCall(value);
        const HavenFSK::RxMeasurement* m =
            m_pipeline->getRxMeasurement(value);
        if (m) {
            QString rs = HavenFSK::DspPipeline::computeRS(*m);
            m_logPanel->setRsSent(rs);
            m_macroPanel->setRsSent(rs);
        }
    }
}


void MainWindow::onContactLogged(const QVariantMap& fields) {
    if (m_logManager && m_logManager->isOpen()) {
        if (!m_logManager->logContact(fields)) {
            m_statusLabel->setText(
                "Warning: contact may not have been saved");
        } else {
            m_statusLabel->setText(
                QString("Logged: %1")
                .arg(fields["their_callsign"].toString()));
        }
    }
}

void MainWindow::onExport() {
    if (!m_logManager || !m_logManager->isOpen()) {
        QMessageBox::warning(this, "Export",
            "Log database is not available.");
        return;
    }
    ExportDialog dlg(m_logManager, this);
    dlg.exec();
}

void MainWindow::onFieldDayToggled(bool enabled) {
    m_logPanel->setFieldDayMode(enabled);
}

void MainWindow::onOpenRadioConfig() {
    RadioConfigDialog dlg(this);
    dlg.setConnected(m_radio && m_radio->isConnected());
    connect(&dlg, &RadioConfigDialog::configChanged,
            this, &MainWindow::startRadio);
    connect(&dlg, &RadioConfigDialog::connectRequested,
            this, &MainWindow::startRadio);
    connect(&dlg, &RadioConfigDialog::disconnectRequested,
            this, &MainWindow::stopRadio);
    dlg.exec();
}

void MainWindow::onModeChanged(int index) {
    if (!m_pipeline || index < 0) return;

    auto mode = static_cast<HavenFSK::ModemMode>(
        m_modeCombo->itemData(index).toInt());
    m_pipeline->setMode(mode);

    QSettings s;
    s.setValue("mode/current", static_cast<int>(mode));

    if (m_waterfall) {
        m_waterfall->setPassband(
            static_cast<float>(m_pipeline->passbandLowHz()),
            static_cast<float>(m_pipeline->passbandHighHz()));
    }

    m_statusLabel->setText("Mode: " + m_pipeline->modeName());
}

void MainWindow::onToneSweepTx() {
    if (m_audio->isTransmitting()) {
        m_statusLabel->setText("TX in progress — wait for it to finish");
        return;
    }

    // Generate the 16-tone sweep audio (does NOT set m_pipeline m_transmitting,
    // so the RX pipeline and tone monitor stay live during playback).
    std::vector<float> audio = m_pipeline->generateToneSweepAudio();

    float gain = 1.0f;
    if (m_levelPanel) {
        float dBFS = m_levelPanel->txFaderDbFS();
        gain = std::max(0.0f, std::min(1.0f, std::pow(10.0f, dBFS / 20.0f)));
    }
    QString outDev = HavenFSK::savedOutputDevice();
    int leadMs = HavenFSK::pttLeadMs();

    m_toneSweepActive = true;
    m_toneTestButton->setEnabled(false);
    m_statusLabel->setText("Tone sweep TX (8s) — watch debug log for ToneMon[] lines");

    // Start tone monitor BEFORE TX so no received symbols are missed.
    m_monitorButton->setChecked(true);   // visually reflects active state

    auto startAudio = [this, audio, outDev, gain]() {
        if (!m_audio->startTx(outDev, audio, gain)) {
            m_toneSweepActive = false;
            m_pipeline->setToneMonitor(false);
            m_toneTestButton->setEnabled(true);
            m_statusLabel->setText("Tone sweep: audio start failed");
        }
    };

    if (m_pttManager && m_radio && m_radio->isConnected()) {
        if (!m_pttManager->requestTX()) {
            m_toneSweepActive = false;
            m_pipeline->setToneMonitor(false);
            m_toneTestButton->setEnabled(true);
            m_statusLabel->setText("Tone sweep: PTT request failed");
            return;
        }
        QTimer::singleShot(leadMs, this, startAudio);
    } else {
        startAudio();
    }
}

void MainWindow::onWaterfallTune(float audioHz) {
    if (!m_radio || !m_radio->isConnected()) {
        m_statusLabel->setText(
            "Connect rig control to enable click-to-tune");
        return;
    }
    uint64_t dialHz = m_radio->getFrequency();
    if (dialHz == 0) {
        qWarning() << "MainWindow::onWaterfallTune: getFrequency() returned 0";
        return;
    }

    // Place lowest HAVEN-FSK tone (BASE_FREQ) at the clicked audio position.
    // newDial = currentDial + (clickedAudioHz - BASE_FREQ)
    int64_t  offset = static_cast<int64_t>(audioHz)
                    - static_cast<int64_t>(HavenFSK::BASE_FREQ);
    uint64_t newHz  = static_cast<uint64_t>(
        static_cast<int64_t>(dialHz) + offset);

    qDebug() << "MainWindow: waterfall tune" << audioHz
             << "Hz audio ->" << newHz << "Hz dial";

    m_radio->setFrequency(newHz);
    m_freqControl->setFrequency(newHz);
    m_statusLabel->setText(
        QString("Tuned to %1 MHz")
        .arg(static_cast<double>(newHz) / 1.0e6, 0, 'f', 6));
}
