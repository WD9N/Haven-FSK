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
#include "../log/LogManager.h"
#include "../audio/AudioEngine.h"
#include "../audio/AudioSettings.h"
#include "../radio/RadioSettings.h"
#include "../radio/RadioInterface.h"
#include "../radio/RigctldClient.h"
#include "../radio/TCIClient.h"
#include "../dsp/DspPipeline.h"
#include "../dsp/Constants.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QMenuBar>
#include <QMenu>
#include <QMessageBox>
#include <QDateTime>
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
    setMinimumSize(800, 640);

    m_audio      = new AudioEngine(this);
    m_pipeline   = new HavenFSK::DspPipeline(this);
    m_logManager = new LogManager(this);
    if (!m_logManager->open()) {
        QMessageBox::warning(this, "Log Database",
            "Could not open log database.\n"
            "Contacts will not be saved this session.\n\n"
            "Database path: " + m_logManager->databasePath());
    }

    setupMenu();
    setupUi();
    setupConnections();

    m_stationInfo->refresh();
    onSettingsChanged();

    startAudio();
    startRadio();
}

MainWindow::~MainWindow() {
    stopRadio();
}

void MainWindow::closeEvent(QCloseEvent* event) {
    QSettings s;
    s.setValue("ui/splitterState", m_splitter->saveState());
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

    // Fix 1: Radio menu with Configure... action
    QMenu* radioMenu = menuBar()->addMenu("&Radio");
    auto* configAction = new QAction("&Configure...", this);
    connect(configAction, &QAction::triggered, this, [this]() {
        RadioConfigDialog dlg(this);
        connect(&dlg, &RadioConfigDialog::configChanged,
                this, &MainWindow::startRadio);
        dlg.exec();
    });
    radioMenu->addAction(configAction);
    radioMenu->addSeparator();
    m_connectRigAction    = new QAction("&Connect Rig", this);
    m_disconnectRigAction = new QAction("&Disconnect Rig", this);
    m_disconnectRigAction->setEnabled(false);
    radioMenu->addAction(m_connectRigAction);
    radioMenu->addAction(m_disconnectRigAction);

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
                    "16-tone MFSK HF Digital Mode<br><br>"
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

    m_splitter->setStretchFactor(0, 1);
    m_splitter->setStretchFactor(1, 3);
    m_splitter->setStretchFactor(2, 2);

    mainLayout->addWidget(m_splitter, 1);

    // Macro buttons — fixed
    m_macroPanel = new MacroPanel(central);
    mainLayout->addWidget(m_macroPanel);

    // TX input — fixed
    auto* txGroup  = new QGroupBox("Transmit", central);
    auto* txLayout = new QHBoxLayout(txGroup);
    m_txInput = new QLineEdit(txGroup);
    m_txInput->setPlaceholderText(
        "Type message and press Transmit or Enter...");
    m_txInput->setFont(QFont("Courier New", 10));
    txLayout->addWidget(m_txInput, 1);
    m_txButton = new QPushButton("Transmit", txGroup);
    m_txButton->setMinimumWidth(100);
    txLayout->addWidget(m_txButton);
    mainLayout->addWidget(txGroup);

    // Status bar — fixed
    auto* statusBar    = new QWidget(central);
    auto* statusLayout = new QHBoxLayout(statusBar);
    statusLayout->setContentsMargins(0, 0, 0, 0);
    statusLayout->setSpacing(8);

    m_dcdLabel = new QLabel("DCD: --");
    m_dcdLabel->setMinimumWidth(70);
    statusLayout->addWidget(m_dcdLabel);

    m_rxStateLabel = new QLabel("Idle");
    m_rxStateLabel->setMinimumWidth(80);
    statusLayout->addWidget(m_rxStateLabel);

    m_freqControl = new FrequencyControl(statusBar);
    statusLayout->addWidget(m_freqControl);

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

    // Restore splitter state
    QSettings s;
    QByteArray splitterState = s.value("ui/splitterState").toByteArray();
    if (!splitterState.isEmpty())
        m_splitter->restoreState(splitterState);
}

void MainWindow::setupConnections() {
    // Settings and export
    connect(m_settingsAction, &QAction::triggered,
            this, &MainWindow::onOpenSettings);
    connect(m_exportAction, &QAction::triggered,
            this, &MainWindow::onExport);

    // Radio menu
    connect(m_connectRigAction, &QAction::triggered,
            this, &MainWindow::startRadio);
    connect(m_disconnectRigAction, &QAction::triggered,
            this, &MainWindow::stopRadio);

    // TX
    connect(m_txButton, &QPushButton::clicked,
            this, &MainWindow::onTransmit);
    connect(m_txInput, &QLineEdit::returnPressed,
            this, &MainWindow::onTransmit);

    // AudioEngine → DspPipeline (AFC-corrected path)
    connect(m_audio, &AudioEngine::rxDataReady,
            m_pipeline, &HavenFSK::DspPipeline::onAudioChunk);
    connect(m_audio, &AudioEngine::txComplete,
            this, &MainWindow::onTxComplete);
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

    // Fix 11: FrequencyControl — always update display and log panel;
    // only send to radio if rig is connected
    connect(m_freqControl, &FrequencyControl::frequencyRequested,
            this, [this](uint64_t hz) {
                m_freqControl->setFrequency(hz);
                if (m_logPanel) m_logPanel->setFrequency(hz);
                if (m_radio && m_radio->isConnected())
                    m_radio->setFrequency(hz);
            });

    // DspPipeline → AudioEngine (TX audio)
    connect(m_pipeline, &HavenFSK::DspPipeline::txAudioReady,
            this, [this](const std::vector<float>& samples) {
                m_audio->startTx(HavenFSK::savedOutputDevice(), samples);
            });

    // DspPipeline → UI
    connect(m_pipeline, &HavenFSK::DspPipeline::messageReceived,
            this, &MainWindow::onMessageReceived);
    connect(m_pipeline, &HavenFSK::DspPipeline::dcdChanged,
            this, &MainWindow::onDcdChanged);
    connect(m_pipeline, &HavenFSK::DspPipeline::rxStateChanged,
            this, &MainWindow::onRxStateChanged);

    // RxDisplay → MainWindow (element clicks)
    connect(m_rxDisplay, &RxDisplay::elementClicked,
            this, &MainWindow::onElementClicked);

    // MacroPanel → TX pipeline
    connect(m_macroPanel, &MacroPanel::macroTriggered,
            this, &MainWindow::onMacroTriggered);

    // TX message → RxDisplay (amber [TX] styling)
    connect(m_pipeline,
            &HavenFSK::DspPipeline::messageTransmitted,
            this, [this](const QString& text) {
                HavenFSK::StationInfo info = HavenFSK::loadStationInfo();
                m_rxDisplay->appendTxMessage(text, info.callsign);
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
            this, &MainWindow::onRadioConnected);
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
    m_connectRigAction->setEnabled(true);
    m_disconnectRigAction->setEnabled(false);
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

    m_audio->stop();
    startAudio();
    startRadio();
}

void MainWindow::onTransmit() {
    HavenFSK::StationInfo info = HavenFSK::loadStationInfo();
    if (info.callsign.isEmpty()) {
        QMessageBox::warning(this, "No Callsign",
            "Please enter your callsign in Settings → Station Info\n"
            "before transmitting.\n\n"
            "Transmitting without identifying by callsign\n"
            "violates FCC Part 97.119.");
        return;
    }

    QString text = m_txInput->text().trimmed();
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
    m_txButton->setEnabled(true);
    m_txInput->setEnabled(true);
    m_txInput->clear();
    m_statusLabel->setText("Listening...");
    m_audio->startRx(HavenFSK::savedInputDevice());
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

void MainWindow::onDcdChanged(bool active) {
    m_dcdLabel->setText(active ? "DCD: ON" : "DCD: --");
    m_dcdLabel->setStyleSheet(
        active ? "color: green; font-weight: bold;" : "");
}

void MainWindow::onRxStateChanged(HavenFSK::RxState state) {
    switch (state) {
    case HavenFSK::RxState::Idle:
        m_rxStateLabel->setText("Idle");      break;
    case HavenFSK::RxState::Searching:
        m_rxStateLabel->setText("Searching"); break;
    case HavenFSK::RxState::Receiving:
        m_rxStateLabel->setText("Receiving"); break;
    }
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
    m_connectRigAction->setEnabled(false);
    m_disconnectRigAction->setEnabled(true);
}

void MainWindow::onRadioDisconnected() {
    m_rigLabel->setText("Rig disconnected");
    m_rigLabel->setStyleSheet("color: orange;");
    m_connectRigAction->setEnabled(true);
    m_disconnectRigAction->setEnabled(false);
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

void MainWindow::onChannelBusy() {
    m_statusLabel->setText("Channel busy — waiting for clear frequency");
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

void MainWindow::onMacroTriggered(const QString& text, bool autoTx) {
    m_txInput->setText(text);
    if (autoTx) onTransmit();
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

void MainWindow::onWaterfallTune(float audioHz) {
    if (!m_radio || !m_radio->isConnected()) {
        m_statusLabel->setText(
            "Connect rig control to enable click-to-tune");
        return;
    }
    uint64_t dialHz = m_radio->getFrequency();
    if (dialHz == 0) return;

    // Place lowest HAVEN-FSK tone (BASE_FREQ) at the clicked audio position.
    // newDial = currentDial + (clickedAudioHz - BASE_FREQ)
    int64_t  offset = static_cast<int64_t>(audioHz)
                    - static_cast<int64_t>(HavenFSK::BASE_FREQ);
    uint64_t newHz  = static_cast<uint64_t>(
        static_cast<int64_t>(dialHz) + offset);

    m_radio->setFrequency(newHz);
    m_freqControl->setFrequency(newHz);
    m_statusLabel->setText(
        QString("Tuned to %1 MHz")
        .arg(static_cast<double>(newHz) / 1.0e6, 0, 'f', 6));
}
