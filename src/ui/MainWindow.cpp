#include "MainWindow.h"
#include <optional>
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
#include "../radio/HamlibClient.h"
#include "../pipeline/DspPipeline.h"
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
#include <QFile>
#include <QCoreApplication>
#include <QStandardPaths>
#include <QDir>

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

    // Move real-time audio capture + DSP/modem processing off the GUI
    // thread, onto their own dedicated worker thread — see DECISIONS.md
    // (ADR-109) for why: GUI-thread work (the waterfall's per-chunk FFT +
    // repaint at Fast speed, in particular) was starving AudioEngine's
    // real-time audio draining, causing silent sample loss mid-transmission.
    // Both objects move together (not split, not separate threads) so
    // AudioEngine::rxDataReady -> DspPipeline::onAudioChunk resolves as a
    // same-thread direct call (the hottest path in the app), and so
    // DspPipeline::setMode()'s m_modem pointer swap can never run
    // concurrently with onAudioChunk() -- a single thread's event loop only
    // ever runs one queued call at a time. Must un-parent first: a QObject
    // with a parent can't be moved to another thread.
    m_audio->setParent(nullptr);
    m_pipeline->setParent(nullptr);
    m_dspThread = new QThread(this);
    m_audio->moveToThread(m_dspThread);
    m_pipeline->moveToThread(m_dspThread);
    m_dspThread->start();

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
    connect(m_pttManager, &PTTManager::pttReleaseFailed,
            this, &MainWindow::onPttReleaseFailed);

    setupMenu();
    setupUi();
    setupConnections();

    // Show today's already-logged contacts — a mid-day restart must not
    // hide them (or leave them un-editable for lack of a db_id).
    if (m_logManager->isOpen()) {
        QString todayUtc = QDateTime::currentDateTimeUtc().toString("yyyyMMdd");
        m_logPanel->loadContacts(m_logManager->contactsForDate(todayUtc));
    }

    // Apply the RESTORED RX fader position to the pipeline — the change
    // signal only fires on operator moves, not on constructor restore.
    // (TX needs no push: its gain is read fresh at each transmit start.)
    m_pipeline->setRxGain(m_levelPanel->rxFaderGain());

    // Restore last-used mode; setting the combo index triggers
    // onModeChanged() -> DspPipeline::setMode(), so this both applies the
    // saved mode and updates the waterfall passband/labels for it.
    {
        QSettings s;
        int savedMode = s.value("mode/current",
            static_cast<int>(HavenFSK::ModemMode::Mfsk16)).toInt();
        int idx = m_modeCombo->findData(savedMode);
        m_modeCombo->setCurrentIndex(idx >= 0 ? idx : 0);

        // Restore squelch before onModeChanged() below, which re-applies
        // whatever m_squelchSpin currently holds to the fresh modem.
        m_squelchSpin->setValue(s.value("mode/squelch", 0.0).toDouble());

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
    // Worker-thread teardown first: m_audio/m_pipeline live on m_dspThread
    // (see the constructor) and are no longer parent-owned (un-parented for
    // the moveToThread() call), so they need an explicit, ordered shutdown
    // rather than relying on QObject's automatic parent-child cleanup.
    // BlockingQueuedConnection (not the usual AutoConnection) deliberately
    // blocks this destructor until stop() has actually finished running on
    // the worker thread, before quit()/wait() tear the thread down and it
    // becomes unsafe to call into these objects at all.
    if (m_dspThread) {
        QMetaObject::invokeMethod(m_audio, &AudioEngine::stop,
                                   Qt::BlockingQueuedConnection);
        m_dspThread->quit();
        m_dspThread->wait();
        delete m_pipeline;
        delete m_audio;
    }
    stopRadio();
}

void MainWindow::closeEvent(QCloseEvent* event) {
    QSettings s;
    s.setValue("ui/dockState1", saveState());
    s.setValue("ui/windowGeometry", saveGeometry());
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
        QMetaObject::invokeMethod(m_pipeline, &HavenFSK::DspPipeline::setAfcEnabled,
                                   Qt::AutoConnection, on);
        if (!on)
            m_waterfall->setAfcOffset(0.0f);
    });

    opMenu->addSeparator();
    m_recordRxAction = new QAction("Record RX Audio to WAV (debug)", this);
    m_recordRxAction->setCheckable(true);
    opMenu->addAction(m_recordRxAction);
    connect(m_recordRxAction, &QAction::toggled,
            this, &MainWindow::onRecordRxToggled);

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
    // Empty central widget — dock areas fill the remaining space around it.
    // Qt6 supports a null central widget too, but an explicit empty one is
    // a more conservative default (verified visually, no seam/margin issue).
    setCentralWidget(new QWidget(this));
    setDockNestingEnabled(true);  // needed for the mixed horizontal/vertical arrangement below

    // Station info — fixed, always visible, pinned above the dock area via
    // a non-movable toolbar (not a dock widget itself, and not part of the
    // dockable set the operator asked for).
    m_topBar = new QToolBar("Station Info", this);
    m_topBar->setObjectName("topBar");  // required for saveState()/restoreState() to work
    m_topBar->setMovable(false);
    m_topBar->setFloatable(false);
    m_topBar->toggleViewAction()->setVisible(false);  // no reason to hide this
    m_stationInfo = new StationInfoWidget(m_topBar);
    m_topBar->addWidget(m_stationInfo);

    // Frequency/mode/squelch/rig/RX — moved here from the bottom status bar
    // (per operator request), to the right of station info.
    m_topBar->addSeparator();

    m_freqControl = new FrequencyControl(m_topBar);
    m_topBar->addWidget(m_freqControl);

    m_modeCombo = new QComboBox(m_topBar);
    m_modeCombo->addItem("Haven MFSK", static_cast<int>(HavenFSK::ModemMode::Mfsk16));
    m_modeCombo->addItem("PSK31",   static_cast<int>(HavenFSK::ModemMode::Psk31));
    m_modeCombo->setToolTip("Operating mode");
    m_topBar->addWidget(m_modeCombo);

    m_topBar->addWidget(new QLabel("Squelch:", m_topBar));
    m_squelchSpin = new QDoubleSpinBox(m_topBar);
    m_squelchSpin->setRange(0.0, 1.0);
    m_squelchSpin->setSingleStep(0.05);
    m_squelchSpin->setDecimals(2);
    m_squelchSpin->setToolTip(
        "Minimum decode confidence to display a character (0 = off).\n"
        "Only affects modes with a confidence-based squelch (PSK31); "
        "has no effect on MFSK, which uses CRC/FEC instead.\n"
        "Raise this if noise is decoding as garbage text; lower it "
        "(or set to 0) if real signal isn't showing up.");
    m_topBar->addWidget(m_squelchSpin);

    m_rigLabel = new QLabel("No rig", m_topBar);
    m_rigLabel->setStyleSheet("color: gray;");
    m_topBar->addWidget(m_rigLabel);

    m_topBar->addWidget(new QLabel("RX:", m_topBar));
    m_rxLevel = new QProgressBar(m_topBar);
    m_rxLevel->setRange(0, 100);
    m_rxLevel->setValue(0);
    m_rxLevel->setMaximumWidth(120);
    m_rxLevel->setTextVisible(false);
    m_topBar->addWidget(m_rxLevel);

    addToolBar(Qt::TopToolBarArea, m_topBar);

    // Waterfall — receives raw audio directly from AudioEngine
    m_waterfall = new WaterfallWidget(this);
    m_waterfall->setMinimumHeight(80);
    m_dockWaterfall = new QDockWidget("Waterfall", this);
    m_dockWaterfall->setObjectName("dockWaterfall");
    m_dockWaterfall->setFeatures(QDockWidget::DockWidgetMovable |
                                 QDockWidget::DockWidgetFloatable);
    m_dockWaterfall->setWidget(m_waterfall);
    addDockWidget(Qt::TopDockWidgetArea, m_dockWaterfall);

    // RX display
    auto* rxContainer = new QWidget(this);
    auto* rxLayout     = new QVBoxLayout(rxContainer);
    rxLayout->setContentsMargins(0, 0, 0, 0);
    rxLayout->setSpacing(2);

    auto* rxBtnRow = new QHBoxLayout;
    rxBtnRow->addStretch();
    auto* rxClearButton = new QPushButton("Clear", rxContainer);
    rxClearButton->setToolTip("Clear the Received window");
    rxBtnRow->addWidget(rxClearButton);
    rxLayout->addLayout(rxBtnRow);

    m_rxDisplay = new RxDisplay(rxContainer);
    m_rxDisplay->setMinimumHeight(100);
    rxLayout->addWidget(m_rxDisplay, 1);

    connect(rxClearButton, &QPushButton::clicked,
            m_rxDisplay, &RxDisplay::clearMessages);

    m_dockReceived = new QDockWidget("Received", this);
    m_dockReceived->setObjectName("dockReceived");
    m_dockReceived->setFeatures(QDockWidget::DockWidgetMovable |
                                QDockWidget::DockWidgetFloatable);
    m_dockReceived->setWidget(rxContainer);
    splitDockWidget(m_dockWaterfall, m_dockReceived, Qt::Vertical);

    // Log panel
    m_logPanel = new LogPanel;
    m_logPanel->setMinimumHeight(120);
    m_dockLog = new QDockWidget("Log", this);
    m_dockLog->setObjectName("dockLog");
    m_dockLog->setFeatures(QDockWidget::DockWidgetMovable |
                           QDockWidget::DockWidgetFloatable);
    m_dockLog->setWidget(m_logPanel);
    splitDockWidget(m_dockReceived, m_dockLog, Qt::Vertical);

    // Level meter — its own dock now (was bundled with macros/TX)
    m_levelPanel = new LevelPanel(this);
    m_dockLevels = new QDockWidget("Levels", this);
    m_dockLevels->setObjectName("dockLevels");
    m_dockLevels->setFeatures(QDockWidget::DockWidgetMovable |
                              QDockWidget::DockWidgetFloatable);
    m_dockLevels->setWidget(m_levelPanel);
    splitDockWidget(m_dockLog, m_dockLevels, Qt::Vertical);

    // Transmit dock — Macro Panel back above the TX input, one container
    // (per operator request, back from being its own separate dock).
    auto* txContainer = new QWidget(this);
    txContainer->setSizePolicy(
        QSizePolicy::Expanding, QSizePolicy::Expanding);

    auto* txOuterLayout = new QVBoxLayout(txContainer);
    txOuterLayout->setContentsMargins(4, 4, 4, 4);
    txOuterLayout->setSpacing(4);

    m_macroPanel = new MacroPanel(txContainer);
    m_macroPanel->setSizePolicy(
        QSizePolicy::Expanding, QSizePolicy::Preferred);
    txOuterLayout->addWidget(m_macroPanel, 0);

    auto* txLayout = new QVBoxLayout;
    txLayout->setContentsMargins(0, 0, 0, 0);
    txLayout->setSpacing(4);
    txOuterLayout->addLayout(txLayout, 1);

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

    auto* txClearButton = new QPushButton("Clear", txContainer);
    txClearButton->setToolTip("Clear the Transmit input");
    connect(txClearButton, &QPushButton::clicked,
            m_txInput, &QTextEdit::clear);
    txBtnRow->addWidget(txClearButton);
    txBtnRow->addSpacing(8);

    m_tuneButton = new QPushButton("Tune", txContainer);
    m_tuneButton->setMinimumWidth(72);
    m_tuneButton->setCheckable(true);
    m_tuneButton->setToolTip(
        "Steady 1000 Hz tone for setting TX level (click again to stop).\n"
        "Read power on an average-reading meter.");
    m_tuneButton->setStyleSheet(
        "QPushButton {"
        "  background: #1a1a2e; color: #7a9ab0;"
        "  border: 1px solid #2a3a4a; border-radius: 3px;"
        "  padding: 4px 12px;"
        "}"
        "QPushButton:hover   { background: #222240; }"
        "QPushButton:checked { background: #2e1a1a; color: #b07a7a;"
        "                      border-color: #4a2a2a; }");
    txBtnRow->addWidget(m_tuneButton);
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

    m_dockTransmit = new QDockWidget("Transmit", this);
    m_dockTransmit->setObjectName("dockTransmit");
    m_dockTransmit->setFeatures(QDockWidget::DockWidgetMovable |
                                QDockWidget::DockWidgetFloatable);
    m_dockTransmit->setWidget(txContainer);
    splitDockWidget(m_dockLevels, m_dockTransmit, Qt::Horizontal);

    // Status bar — fixed, pinned below the dock area via a non-movable
    // toolbar (same reasoning as m_topBar above). Frequency/mode/squelch/
    // rig/RX moved up to m_topBar (per operator request) — only the
    // free-text status message stays down here.
    m_bottomBar = new QToolBar("Status", this);
    m_bottomBar->setObjectName("bottomBar");
    m_bottomBar->setMovable(false);
    m_bottomBar->setFloatable(false);
    m_bottomBar->toggleViewAction()->setVisible(false);

    m_statusLabel = new QLabel("Ready", m_bottomBar);
    m_bottomBar->addWidget(m_statusLabel);
    addToolBar(Qt::BottomToolBarArea, m_bottomBar);

    // Restore dock layout (replaces the old splitter-state mechanism —
    // ui/splitterState4 is no longer written or read). The
    // splitDockWidget() calls above already establish a sensible default
    // arrangement resembling the pre-dock layout, so restoreState() only
    // needs to run when the operator has actually rearranged something.
    // Window position/size is separate from dock/toolbar layout — saveState()/
    // restoreState() only cover the latter — so it's saved and restored
    // independently via saveGeometry()/restoreGeometry().
    QSettings s;
    QByteArray windowGeometry = s.value("ui/windowGeometry").toByteArray();
    if (!windowGeometry.isEmpty())
        restoreGeometry(windowGeometry);

    QByteArray dockState = s.value("ui/dockState1").toByteArray();
    if (!dockState.isEmpty()) {
        restoreState(dockState);
    } else {
        // Qt's dock layout occasionally leaves one or more of these
        // floating on first show when splitDockWidget() is called during
        // construction, before the window has ever been shown/laid out —
        // observed after reworking the default arrangement above. Force
        // them back into the tiled layout the splits above just built.
        m_dockWaterfall->setFloating(false);
        m_dockReceived->setFloating(false);
        m_dockLog->setFloating(false);
        m_dockLevels->setFloating(false);
        m_dockTransmit->setFloating(false);
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
    connect(m_tuneButton, &QPushButton::toggled,
            this, &MainWindow::onTuneToggled);
    // QTextEdit: Ctrl+Enter transmits, plain Enter adds newline
    auto* txShortcut = new QShortcut(
        QKeySequence(Qt::CTRL | Qt::Key_Return), m_txInput);
    connect(txShortcut, &QShortcut::activated,
            this, &MainWindow::onTransmit);

    // AudioEngine → DspPipeline (AFC-corrected path)
    connect(m_audio, &AudioEngine::rxDataReady,
            m_pipeline, &HavenFSK::DspPipeline::onAudioChunk);
    // Debug RX capture — see onRecordRxToggled(). Taps the exact same
    // signal/samples the modem receives, not a separately-recorded file.
    // Bulk float insert only — no per-sample conversion on the real-time
    // path (see m_rxRecordBuffer's doc comment in MainWindow.h).
    connect(m_audio, &AudioEngine::rxDataReady,
            this, [this](const std::vector<float>& samples) {
                if (!m_recordingRx) return;
                m_rxRecordBuffer.insert(m_rxRecordBuffer.end(),
                                         samples.begin(), samples.end());
                if (static_cast<int>(m_rxRecordBuffer.size()) >= RX_RECORD_MAX_SAMPLES) {
                    m_statusLabel->setText(
                        "RX recording hit 5 min cap — saved to rx_capture.wav");
                    m_recordRxAction->setChecked(false);  // triggers save via onRecordRxToggled
                }
            });
    connect(m_audio, &AudioEngine::txComplete,
            this, &MainWindow::onTxComplete);
    // Pipeline clears its transmitting flag when audio finishes.
    // This fires before the tail delay — m_pipeline is ready for
    // the next transmit while we wait for PTT to release.
    connect(m_audio, &AudioEngine::txComplete,
            m_pipeline, &HavenFSK::DspPipeline::onTxComplete);
    connect(m_audio, &AudioEngine::audioError,
            this, &MainWindow::onAudioError);
    // Separate handler for the specific case of startTx() failing right
    // after PTT lead time — reacts to the same signal rather than startTx()'s
    // bool return, since that becomes unusable once startTx() is invoked via
    // a queued cross-thread call. Guarded by m_awaitingTxStart so unrelated
    // audioError emissions (e.g. an RX format mismatch) aren't misattributed
    // as a TX-start failure.
    connect(m_audio, &AudioEngine::audioError,
            this, &MainWindow::onTxStartError);
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
    connect(m_pipeline, &HavenFSK::DspPipeline::modeReady,
            this, &MainWindow::onModeReady);

    connect(m_squelchSpin, &QDoubleSpinBox::valueChanged,
            this, &MainWindow::onSquelchChanged);

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

    connect(m_pipeline, &HavenFSK::DspPipeline::tuneAudioReady,
            this, &MainWindow::onTuneAudioReady);

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
                        QMetaObject::invokeMethod(m_pipeline, &HavenFSK::DspPipeline::onTxComplete,
                                                   Qt::AutoConnection);
                        onTxComplete();
                        return;
                    }
                    qDebug() << "TX: PTT asserted, waiting"
                             << leadMs << "ms lead time";
                    QTimer::singleShot(leadMs, this,
                        [this, samples, outDev, initialGain]() {
                            qDebug() << "TX: lead time elapsed, starting audio";
                            // Failure recovery happens in onTxStartError(),
                            // connected to AudioEngine::audioError — not via
                            // startTx()'s bool return (see connect() comment
                            // in setupConnections() for why).
                            m_awaitingTxStart = true;
                            QMetaObject::invokeMethod(m_audio, &AudioEngine::startTx,
                                                       Qt::AutoConnection,
                                                       outDev, samples, initialGain);
                        });
                } else {
                    // No rig control — VOX mode
                    qDebug() << "TX: no rig control — VOX mode";
                    m_awaitingTxStart = true;
                    QMetaObject::invokeMethod(m_audio, &AudioEngine::startTx,
                                               Qt::AutoConnection,
                                               outDev, samples, initialGain);
                }
            });

    // DspPipeline → UI
    connect(m_pipeline, &HavenFSK::DspPipeline::messageReceived,
            this, &MainWindow::onMessageReceived);

    connect(m_pipeline, &HavenFSK::DspPipeline::textCharacterReceived,
            m_rxDisplay, &RxDisplay::appendStreamingText);

    // Carrier drop = natural break between transmissions for a
    // continuous-stream mode (PSK31) — start the next one on a fresh
    // line rather than running on from the last.
    connect(m_pipeline, &HavenFSK::DspPipeline::dcdChanged,
            this, [this](bool active) {
                if (!active) m_rxDisplay->endStreamingLine();
            });

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

    // Adaptive sync threshold — keep the operator informed when the
    // receiver defends itself against false syncs (or relaxes again).
    connect(m_pipeline, &HavenFSK::DspPipeline::syncThresholdChanged,
            this, [this](float threshold, const QString& reason) {
                m_statusLabel->setText(
                    QString("Sync threshold %1 — %2")
                        .arg(threshold, 0, 'f', 2).arg(reason));
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

    connect(m_pipeline, &HavenFSK::DspPipeline::transmitFailed,
            this, &MainWindow::onTransmitFailed);

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

    // Database id of the freshly-saved row back to the table row that was
    // just added for it — edits/deletes of session rows need it to reach
    // the database (they're gated on db_id > 0).
    connect(m_logManager, &LogManager::contactSaved,
            m_logPanel, &LogPanel::onContactPersisted);

    // Log panel's "Their Call" entry (typed or populated by clicking a
    // received callsign) drives <theirCall> in macros too, not just clicks.
    connect(m_logPanel, &LogPanel::theirCallChanged,
            this, [this](const QString& call) {
                m_macroPanel->setTheirCall(call);
            });

    // RS-S field drives <rstSent> the same way — whatever the log entry
    // shows (typed, auto-computed, clicked, or cleared) is what macros
    // expand, fixing <rstSent> going stale/empty when the report was
    // entered manually.
    connect(m_logPanel, &LogPanel::rsSentChanged,
            this, [this](const QString& rs) {
                m_macroPanel->setRsSent(rs);
            });

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
    // Failure is reported via the existing onAudioError() (connected to
    // AudioEngine::audioError, which startRx()'s failure paths already
    // emit) rather than a synchronous bool check, which becomes unusable
    // once startRx() is invoked via a queued cross-thread call.
    m_statusLabel->setText("Listening...");
    QMetaObject::invokeMethod(m_audio, &AudioEngine::startRx,
                               Qt::AutoConnection, inDev);
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
    } else if (method == "hamlib") {
        m_radio = new HamlibClient(
            HavenFSK::hamlibPort(),
            HavenFSK::hamlibBaud(),
            HavenFSK::hamlibRigModel(), this);
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
                connect(m_pttManager, &PTTManager::pttReleaseFailed,
                        this, &MainWindow::onPttReleaseFailed);
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
    connect(m_radio, &RadioInterface::connectFailed,
            this, &MainWindow::onRadioConnectFailed);

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
    QMetaObject::invokeMethod(m_audio, &AudioEngine::stop, Qt::AutoConnection);
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

    QMetaObject::invokeMethod(m_audio, &AudioEngine::stopRx, Qt::AutoConnection);
    m_txButton->setEnabled(false);
    m_txInput->setEnabled(false);
    m_statusLabel->setText("Transmitting...");

    // Failure recovery happens in onTransmitFailed(), connected to
    // DspPipeline::transmitFailed — not via transmit()'s bool return, since
    // that becomes unusable once transmit() is invoked via a queued
    // cross-thread call (see plan doc for the worker-thread move).
    QMetaObject::invokeMethod(m_pipeline, &HavenFSK::DspPipeline::transmit,
                               Qt::AutoConnection, text);
}

void MainWindow::onTransmitFailed(const QString& reason) {
    m_statusLabel->setText("TX failed: " + reason);
    m_txButton->setEnabled(true);
    m_txInput->setEnabled(true);
    QMetaObject::invokeMethod(m_audio, &AudioEngine::startRx, Qt::AutoConnection,
                               HavenFSK::savedInputDevice());
}

void MainWindow::onTxStartError(const QString& message) {
    if (!m_awaitingTxStart) return;  // some other, unrelated audioError
    m_awaitingTxStart = false;
    qWarning() << "TX: startTx failed —" << message << "— aborting";

    if (m_tuneActive) {
        // Tune-specific cleanup, distinct from onTxComplete()'s
        // success-path tune branch. Clear m_tuneActive first so
        // setChecked(false)'s toggled handler early-outs instead of
        // repeating the stopTx/PTT-off done here.
        m_tuneActive = false;
        m_tuneButton->setChecked(false);
        m_statusLabel->setText("Tune: audio start failed");
        if (m_pttManager) m_pttManager->txOff();
        if (m_levelPanel) m_levelPanel->setTxLevel(-96.0f);
        QMetaObject::invokeMethod(m_audio, &AudioEngine::startRx, Qt::AutoConnection,
                                   HavenFSK::savedInputDevice());
        return;
    }

    QMetaObject::invokeMethod(m_pipeline, &HavenFSK::DspPipeline::onTxComplete,
                               Qt::AutoConnection);
    onTxComplete();
    if (m_pttManager) m_pttManager->txOff();
}

void MainWindow::onTxComplete() {
    m_awaitingTxStart = false;  // TX genuinely started (or this is the tune/failure cleanup path)

    // Tune tone hit its max length without the operator toggling off.
    // Unchecking triggers onTuneToggled(false), which releases PTT and
    // updates status — don't restart audio (RX was never stopped).
    if (m_tuneActive) {
        m_tuneButton->setChecked(false);
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
        QMetaObject::invokeMethod(m_audio, &AudioEngine::startRx, Qt::AutoConnection,
                                   HavenFSK::savedInputDevice());
    });
}

void MainWindow::onMessageReceived(const HavenFSK::RxMessage& msg) {
    m_rxDisplay->appendMessage(
        msg.text,
        msg.senderCallsign,
        QDateTime::currentDateTimeUtc(),
        msg.crcOk,
        msg.converged);

    // Only feed confirmed-good decodes into the log entry -- a CRC/FEC
    // failure's text may be corrupted and shouldn't seed field values.
    if (msg.crcOk) {
        m_logPanel->autoPopulateFromMessage(msg.senderCallsign, msg.text);

        // RS-S with zero clicks: if this message's station is the one now
        // in the log entry (autoPopulate just verified/filled that), fill
        // an empty RS-S from the measurement cache — same computation as
        // the callsign-click path in onElementClicked().
        if (!msg.senderCallsign.isEmpty()) {
            std::optional<HavenFSK::RxMeasurement> m =
                m_pipeline->getRxMeasurement(msg.senderCallsign);
            if (m) {
                // MacroPanel syncs via LogPanel::rsSentChanged if applied.
                m_logPanel->maybeSetAutoRsSent(
                    msg.senderCallsign,
                    HavenFSK::DspPipeline::computeRS(*m));
            }
        }
    }

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

void MainWindow::onRadioConnectFailed(const QString& reason) {
    // Distinct from onRadioDisconnected(): this means the connection
    // never succeeded at all and has stopped retrying automatically —
    // e.g. a wrong COM port/host in Radio -> Configure — rather than a
    // previously-working connection that dropped and is still retrying
    // in the background. Surfaced in red (vs. disconnected's orange) so
    // it reads as "needs operator action", not "will reconnect itself".
    m_rigLabel->setText("Rig: connection failed");
    m_rigLabel->setStyleSheet("color: red;");
    m_statusLabel->setText(reason);
    m_freqControl->setFrequency(0);
}

void MainWindow::onRecordRxToggled(bool on) {
    if (on) {
        m_rxRecordBuffer.clear();
        m_rxRecordBuffer.reserve(static_cast<size_t>(RX_RECORD_MAX_SAMPLES));
        m_recordingRx = true;
        m_statusLabel->setText(
            "Recording RX audio to rx_capture.wav (up to 5 min)…");
    } else {
        m_recordingRx = false;
        saveRxRecording();
    }
}

void MainWindow::saveRxRecording() {
    if (m_rxRecordBuffer.empty()) return;

    // Next to the exe when possible (portable convention); per-user
    // AppData when the exe dir isn't writable (e.g. unzipped into
    // Program Files). Attempting the open is the reliable writability
    // test on Windows — directory-permission queries don't reflect ACLs.
    QString path =
        QCoreApplication::applicationDirPath() + "/rx_capture.wav";

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        const QString dataDir = QStandardPaths::writableLocation(
            QStandardPaths::AppDataLocation);
        QDir().mkpath(dataDir);
        path = dataDir + "/rx_capture.wav";
        f.setFileName(path);
        if (!f.open(QIODevice::WriteOnly)) {
            qWarning() << "MainWindow: could not open" << path << "for writing";
            m_statusLabel->setText("Failed to save rx_capture.wav — see debug log");
            m_rxRecordBuffer.clear();
            return;
        }
    }

    // Standard 44-byte PCM WAV header — mono, 16-bit, 48000 Hz, matching
    // exactly what AudioEngine::rxDataReady delivers (see ADR-100).
    // float -> int16 conversion happens here, at save time, off the
    // real-time capture path (see m_rxRecordBuffer's doc comment in
    // MainWindow.h).
    std::vector<int16_t> pcm(m_rxRecordBuffer.size());
    for (size_t i = 0; i < m_rxRecordBuffer.size(); ++i) {
        float clamped = std::max(-1.0f, std::min(1.0f, m_rxRecordBuffer[i]));
        pcm[i] = static_cast<int16_t>(clamped * 32767.0f);
    }

    const uint32_t sampleRate   = static_cast<uint32_t>(HavenFSK::SAMPLE_RATE);
    const uint16_t numChannels  = 1;
    const uint16_t bitsPerSample = 16;
    const uint32_t byteRate     = sampleRate * numChannels * bitsPerSample / 8;
    const uint16_t blockAlign   = numChannels * bitsPerSample / 8;
    const uint32_t dataSize     =
        static_cast<uint32_t>(pcm.size() * sizeof(int16_t));
    const uint32_t riffSize     = 36 + dataSize;

    auto writeU32 = [&f](uint32_t v) {
        char b[4] = { char(v & 0xFF), char((v >> 8) & 0xFF),
                      char((v >> 16) & 0xFF), char((v >> 24) & 0xFF) };
        f.write(b, 4);
    };
    auto writeU16 = [&f](uint16_t v) {
        char b[2] = { char(v & 0xFF), char((v >> 8) & 0xFF) };
        f.write(b, 2);
    };

    f.write("RIFF", 4);
    writeU32(riffSize);
    f.write("WAVE", 4);
    f.write("fmt ", 4);
    writeU32(16);       // fmt chunk size
    writeU16(1);        // PCM
    writeU16(numChannels);
    writeU32(sampleRate);
    writeU32(byteRate);
    writeU16(blockAlign);
    writeU16(bitsPerSample);
    f.write("data", 4);
    writeU32(dataSize);
    f.write(reinterpret_cast<const char*>(pcm.data()),
            static_cast<qint64>(dataSize));
    f.close();

    qDebug() << "MainWindow: saved" << m_rxRecordBuffer.size()
             << "samples (" << (m_rxRecordBuffer.size() / (double)HavenFSK::SAMPLE_RATE)
             << "s) to" << path;
    m_statusLabel->setText(
        QString("Saved %1s RX recording to %2")
        .arg(m_rxRecordBuffer.size() / (double)HavenFSK::SAMPLE_RATE, 0, 'f', 1)
        .arg(path));

    m_rxRecordBuffer.clear();
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

void MainWindow::onPttReleaseFailed() {
    // A rig that won't unkey is the one failure that must interrupt the
    // operator — it transmits a dead carrier until physically stopped.
    m_statusLabel->setText("PTT RELEASE FAILED — CHECK YOUR RIG");
    QMessageBox::critical(this, "PTT Release Failed",
        "The command to stop transmitting could not be confirmed.\n\n"
        "YOUR RADIO MAY STILL BE TRANSMITTING.\n\n"
        "Check the rig's TX indicator now. If it is still keyed, unkey it "
        "manually (power off if necessary), then check the rig control "
        "connection (rigctld/TCI) before transmitting again.");
}


void MainWindow::onElementClicked(const QString& scheme, const QString& value) {
    m_logPanel->populateField(scheme, value);

    if (scheme == "callsign") {
        m_macroPanel->setTheirCall(value);
        std::optional<HavenFSK::RxMeasurement> m =
            m_pipeline->getRxMeasurement(value);
        if (m) {
            // MacroPanel picks this up via LogPanel::rsSentChanged.
            m_logPanel->setRsSent(HavenFSK::DspPipeline::computeRS(*m));
        }
    }
}


void MainWindow::onContactLogged(const QVariantMap& fields) {
    if (m_logManager && m_logManager->isOpen()) {
        QVariantMap stamped = fields;
        stamped["modem_name"] = m_currentModeName;
        if (!m_logManager->logContact(stamped)) {
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

    // startRadio() destroys and rebuilds m_radio, so live feedback has to
    // be (re)wired to whatever m_radio ends up being *after* each call —
    // wiring it once up front would only cover an already-active m_radio
    // from before the dialog opened, not a fresh Connect click inside it.
    // Auto-disconnects when dlg is destroyed at the end of exec().
    auto startRadioAndWire = [this, &dlg]() {
        startRadio();
        if (m_radio) {
            connect(m_radio, &RadioInterface::connected,
                    &dlg, &RadioConfigDialog::onConnectSucceeded);
            connect(m_radio, &RadioInterface::connectFailed,
                    &dlg, &RadioConfigDialog::onConnectFailed);
        }
    };

    connect(&dlg, &RadioConfigDialog::configChanged,
            this, startRadioAndWire);
    connect(&dlg, &RadioConfigDialog::connectRequested,
            this, startRadioAndWire);
    connect(&dlg, &RadioConfigDialog::disconnectRequested,
            this, &MainWindow::stopRadio);

    if (m_radio) {
        connect(m_radio, &RadioInterface::connected,
                &dlg, &RadioConfigDialog::onConnectSucceeded);
        connect(m_radio, &RadioInterface::connectFailed,
                &dlg, &RadioConfigDialog::onConnectFailed);
    }
    dlg.exec();
}

void MainWindow::onModeChanged(int index) {
    if (!m_pipeline || index < 0) return;

    if (m_rxDisplay) m_rxDisplay->endStreamingLine();

    auto mode = static_cast<HavenFSK::ModemMode>(
        m_modeCombo->itemData(index).toInt());

    QSettings s;
    s.setValue("mode/current", static_cast<int>(mode));

    // Waterfall passband/squelch/status-label updates happen in
    // onModeReady(), connected to DspPipeline::modeReady — not read back
    // synchronously here via passbandLowHz()/passbandHighHz()/modeName()
    // right after this call, since setMode() constructs a fresh IModem and
    // those reads become unsafe once setMode() runs on a different thread
    // than this caller.
    // ModemConfig{} passed explicitly — invokeMethod's function-pointer
    // overload can't rely on setMode()'s default argument.
    QMetaObject::invokeMethod(m_pipeline, &HavenFSK::DspPipeline::setMode,
                               Qt::AutoConnection, mode, HavenFSK::ModemConfig{});
}

void MainWindow::onModeReady(HavenFSK::ModemMode /*mode*/, double loHz,
                              double hiHz, const QString& modeName) {
    if (m_waterfall)
        m_waterfall->setPassband(static_cast<float>(loHz),
                                  static_cast<float>(hiHz));

    // setMode() constructs a fresh IModem instance, which loses any
    // previously-set squelch threshold — re-apply the saved value so
    // switching modes and back doesn't silently reset it. Done here (after
    // the new modem genuinely exists) rather than immediately after calling
    // setMode(), to avoid racing the fresh-modem construction.
    QMetaObject::invokeMethod(m_pipeline, &HavenFSK::DspPipeline::setSquelchThreshold,
                               Qt::AutoConnection,
                               static_cast<float>(m_squelchSpin->value()));

    m_currentModeName = modeName;
    m_statusLabel->setText("Mode: " + modeName);
}

void MainWindow::onSquelchChanged(double value) {
    if (!m_pipeline) return;
    QMetaObject::invokeMethod(m_pipeline, &HavenFSK::DspPipeline::setSquelchThreshold,
                               Qt::AutoConnection, static_cast<float>(value));
    QSettings s;
    s.setValue("mode/squelch", value);
}

void MainWindow::onTuneToggled(bool on) {
    if (on) {
        if (m_audio->isTransmitting()) {
            m_statusLabel->setText("TX in progress — wait for it to finish");
            m_tuneButton->setChecked(false);  // re-enters here; m_tuneActive
            return;                           // still false, so it early-outs
        }
        m_tuneActive = true;
        // Same sequence as a real transmission (onTransmit): RX stops
        // before TX audio starts, so the tone goes out the TX device only.
        QMetaObject::invokeMethod(m_audio, &AudioEngine::stopRx, Qt::AutoConnection);
        m_statusLabel->setText(
            "Tune — 1000 Hz tone, adjust TX level slider (click Tune to stop)");

        // Audio generation happens asynchronously — see onTuneAudioReady(),
        // connected to DspPipeline::tuneAudioReady. Not a direct
        // return-value read, since that becomes unsafe once this runs on a
        // different thread than the caller.
        QMetaObject::invokeMethod(m_pipeline, &HavenFSK::DspPipeline::requestTuneAudio,
                                   Qt::AutoConnection);
    } else {
        if (!m_tuneActive) return;  // failure paths already cleaned up
        m_tuneActive = false;
        QMetaObject::invokeMethod(m_audio, &AudioEngine::stopTx,
                                   Qt::AutoConnection);
        if (m_pttManager) m_pttManager->txOff();
        if (m_levelPanel) m_levelPanel->setTxLevel(-96.0f);
        QMetaObject::invokeMethod(m_audio, &AudioEngine::startRx, Qt::AutoConnection,
                                   HavenFSK::savedInputDevice());
        m_statusLabel->setText("Tune off");
    }
}

void MainWindow::onTuneAudioReady(const std::vector<float>& audio) {
    if (!m_tuneActive) return;  // stale/cancelled request

    float gain = 1.0f;
    if (m_levelPanel) {
        float dBFS = m_levelPanel->txFaderDbFS();
        gain = std::max(0.0f, std::min(1.0f, std::pow(10.0f, dBFS / 20.0f)));
        m_levelPanel->setTxLevel(dBFS);
    }
    QString outDev = HavenFSK::savedOutputDevice();
    int leadMs = HavenFSK::pttLeadMs();

    // Failure recovery happens in onTxStartError() (its m_tuneActive
    // branch), connected to AudioEngine::audioError — not via startTx()'s
    // bool return.
    auto startAudio = [this, audio, outDev, gain]() {
        if (!m_tuneActive) return;  // operator toggled off during PTT lead
        m_awaitingTxStart = true;
        QMetaObject::invokeMethod(m_audio, &AudioEngine::startTx,
                                   Qt::AutoConnection, outDev, audio, gain);
    };

    if (m_pttManager && m_radio && m_radio->isConnected()) {
        if (!m_pttManager->requestTX()) {
            m_tuneActive = false;
            m_tuneButton->setChecked(false);
            if (m_levelPanel) m_levelPanel->setTxLevel(-96.0f);
            QMetaObject::invokeMethod(m_audio, &AudioEngine::startRx, Qt::AutoConnection,
                                       HavenFSK::savedInputDevice());
            m_statusLabel->setText("Tune: PTT request failed");
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
