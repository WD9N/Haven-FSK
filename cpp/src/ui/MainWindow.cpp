#include "MainWindow.h"
#include "SettingsDialog.h"
#include "StationInfoWidget.h"
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
#include <QDebug>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(
        QString("%1 v%2")
        .arg(HavenFSK::APP_NAME)
        .arg(HavenFSK::APP_VERSION));
    setMinimumSize(760, 560);

    m_audio    = new AudioEngine(this);
    m_pipeline = new HavenFSK::DspPipeline(this);

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

void MainWindow::setupMenu() {
    // File menu
    QMenu* fileMenu  = menuBar()->addMenu("&File");
    m_settingsAction = new QAction("&Settings...", this);
    m_settingsAction->setShortcut(Qt::CTRL | Qt::Key_Comma);
    fileMenu->addAction(m_settingsAction);
    fileMenu->addSeparator();
    auto* quitAction = new QAction("&Quit", this);
    quitAction->setShortcut(Qt::CTRL | Qt::Key_Q);
    connect(quitAction, &QAction::triggered, this, &QWidget::close);
    fileMenu->addAction(quitAction);

    // Radio menu
    QMenu* radioMenu       = menuBar()->addMenu("&Radio");
    m_connectRigAction     = new QAction("&Connect Rig", this);
    m_disconnectRigAction  = new QAction("&Disconnect Rig", this);
    m_disconnectRigAction->setEnabled(false);
    radioMenu->addAction(m_connectRigAction);
    radioMenu->addAction(m_disconnectRigAction);

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
    mainLayout->setSpacing(6);

    // Station information block — always visible at top
    m_stationInfo = new StationInfoWidget(central);
    mainLayout->addWidget(m_stationInfo);

    // RX text display
    auto* rxGroup  = new QGroupBox("Received", central);
    auto* rxLayout = new QVBoxLayout(rxGroup);
    m_rxText = new QTextEdit(rxGroup);
    m_rxText->setReadOnly(true);
    m_rxText->setFont(QFont("Courier New", 10));
    m_rxText->setMinimumHeight(180);
    rxLayout->addWidget(m_rxText);
    mainLayout->addWidget(rxGroup, 1);

    // TX input
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

    // Status bar
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

    m_freqLabel = new QLabel("-- MHz");
    m_freqLabel->setFont(QFont("Courier New", 10));
    m_freqLabel->setMinimumWidth(110);
    statusLayout->addWidget(m_freqLabel);

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
}

void MainWindow::setupConnections() {
    // Settings
    connect(m_settingsAction, &QAction::triggered,
            this, &MainWindow::onOpenSettings);

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

    // AudioEngine → DspPipeline
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
    m_freqLabel->setText("-- MHz");
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

    // FCC compliance guard — disable TX if no callsign
    HavenFSK::StationInfo info = HavenFSK::loadStationInfo();
    bool hasCall = !info.callsign.isEmpty();
    m_txButton->setEnabled(hasCall);
    m_txInput->setEnabled(hasCall);
    if (!hasCall)
        m_statusLabel->setText(
            "⚠  Enter callsign in Settings before transmitting");
    else
        m_statusLabel->setText("Listening...");

    // Restart audio with potentially new device
    m_audio->stop();
    startAudio();

    // Restart radio with potentially new settings
    startRadio();
}

void MainWindow::onTransmit() {
    // FCC Part 97 compliance guard
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
    QString ts   = QDateTime::currentDateTimeUtc().toString("hh:mm:ss");
    QString line = QString("[%1] %2").arg(ts).arg(msg.text);
    if (!msg.crcOk)     line += " [CRC FAIL]";
    if (!msg.converged) line += " [FEC NC]";
    m_rxText->append(line);
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
    m_freqLabel->setText("-- MHz");
}

void MainWindow::onFrequencyChanged(uint64_t hz) {
    m_freqLabel->setText(formatFrequency(hz));
}

QString MainWindow::formatFrequency(uint64_t hz) const {
    return QString("%1 MHz")
        .arg(static_cast<double>(hz) / 1e6, 0, 'f', 6);
}

void MainWindow::onWatchdogTripped() {
    QMessageBox::warning(this, "TX Watchdog Tripped",
        "Transmission exceeded 120 seconds and was stopped.\n"
        "Check your audio and radio configuration.");
    onTxComplete();
}

void MainWindow::onChannelBusy() {
    m_statusLabel->setText(
        "Channel busy — waiting for clear frequency");
}
