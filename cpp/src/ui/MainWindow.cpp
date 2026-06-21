#include "MainWindow.h"
#include "../audio/AudioEngine.h"
#include "../audio/AudioSettings.h"
#include "../dsp/DspPipeline.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QGroupBox>
#include <QMenuBar>
#include <QMessageBox>
#include <QSettings>
#include <QDateTime>
#include <QFont>
#include <QDebug>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle("HAVEN-FSK");
    setMinimumSize(700, 500);

    m_audio    = new AudioEngine(this);
    m_pipeline = new HavenFSK::DspPipeline(this);

    setupUi();
    setupConnections();
    startAudio();
}

MainWindow::~MainWindow() = default;

void MainWindow::setupUi() {
    QWidget* central = new QWidget(this);
    setCentralWidget(central);
    QVBoxLayout* mainLayout = new QVBoxLayout(central);

    // ── Audio device selectors ────────────────────────────────────────────
    QGroupBox* audioGroup = new QGroupBox("Audio Devices", central);
    QHBoxLayout* audioLayout = new QHBoxLayout(audioGroup);

    audioLayout->addWidget(new QLabel("Input:"));
    m_inputDevCombo = new QComboBox(audioGroup);
    m_inputDevCombo->addItems(AudioEngine::availableInputDevices());
    QString savedIn = HavenFSK::savedInputDevice();
    if (!savedIn.isEmpty()) {
        int idx = m_inputDevCombo->findText(savedIn);
        if (idx >= 0) m_inputDevCombo->setCurrentIndex(idx);
    }
    audioLayout->addWidget(m_inputDevCombo, 1);

    audioLayout->addWidget(new QLabel("Output:"));
    m_outputDevCombo = new QComboBox(audioGroup);
    m_outputDevCombo->addItems(AudioEngine::availableOutputDevices());
    QString savedOut = HavenFSK::savedOutputDevice();
    if (!savedOut.isEmpty()) {
        int idx = m_outputDevCombo->findText(savedOut);
        if (idx >= 0) m_outputDevCombo->setCurrentIndex(idx);
    }
    audioLayout->addWidget(m_outputDevCombo, 1);

    mainLayout->addWidget(audioGroup);

    // ── RX text display ───────────────────────────────────────────────────
    QGroupBox* rxGroup = new QGroupBox("Received", central);
    QVBoxLayout* rxLayout = new QVBoxLayout(rxGroup);
    m_rxText = new QTextEdit(rxGroup);
    m_rxText->setReadOnly(true);
    m_rxText->setFont(QFont("Courier New", 10));
    rxLayout->addWidget(m_rxText);
    mainLayout->addWidget(rxGroup, 1);

    // ── TX input ──────────────────────────────────────────────────────────
    QGroupBox* txGroup = new QGroupBox("Transmit", central);
    QHBoxLayout* txLayout = new QHBoxLayout(txGroup);
    m_txInput = new QLineEdit(txGroup);
    m_txInput->setPlaceholderText("Type message and press Transmit or Enter");
    m_txInput->setFont(QFont("Courier New", 10));
    txLayout->addWidget(m_txInput, 1);
    m_txButton = new QPushButton("Transmit", txGroup);
    m_txButton->setMinimumWidth(100);
    txLayout->addWidget(m_txButton);
    mainLayout->addWidget(txGroup);

    // ── Status bar ────────────────────────────────────────────────────────
    QWidget* statusWidget = new QWidget(central);
    QHBoxLayout* statusLayout = new QHBoxLayout(statusWidget);
    statusLayout->setContentsMargins(0, 0, 0, 0);

    m_dcdLabel = new QLabel("DCD: --", statusWidget);
    m_dcdLabel->setMinimumWidth(80);
    statusLayout->addWidget(m_dcdLabel);

    m_rxStateLabel = new QLabel("Idle", statusWidget);
    m_rxStateLabel->setMinimumWidth(80);
    statusLayout->addWidget(m_rxStateLabel);

    statusLayout->addWidget(new QLabel("RX Level:"));
    m_rxLevel = new QProgressBar(statusWidget);
    m_rxLevel->setRange(0, 100);
    m_rxLevel->setValue(0);
    m_rxLevel->setMaximumWidth(150);
    m_rxLevel->setTextVisible(false);
    statusLayout->addWidget(m_rxLevel);

    statusLayout->addStretch();

    m_statusLabel = new QLabel("Ready", statusWidget);
    statusLayout->addWidget(m_statusLabel);

    mainLayout->addWidget(statusWidget);
}

void MainWindow::setupConnections() {
    // TX button and Enter key
    connect(m_txButton, &QPushButton::clicked,
            this, &MainWindow::onTransmit);
    connect(m_txInput, &QLineEdit::returnPressed,
            this, &MainWindow::onTransmit);

    // Audio device selection persistence
    connect(m_inputDevCombo, &QComboBox::currentTextChanged,
            this, [](const QString& name) {
                HavenFSK::saveInputDevice(name);
            });
    connect(m_outputDevCombo, &QComboBox::currentTextChanged,
            this, [](const QString& name) {
                HavenFSK::saveOutputDevice(name);
            });

    // AudioEngine → DspPipeline (RX audio flow)
    connect(m_audio, &AudioEngine::rxDataReady,
            m_pipeline, &HavenFSK::DspPipeline::onAudioChunk);

    // AudioEngine → MainWindow
    connect(m_audio, &AudioEngine::txComplete,
            this, &MainWindow::onTxComplete);
    connect(m_audio, &AudioEngine::audioError,
            this, &MainWindow::onAudioError);
    connect(m_audio, &AudioEngine::rxLevelChanged,
            this, &MainWindow::onRxLevelChanged);

    // DspPipeline → AudioEngine (TX audio flow)
    connect(m_pipeline, &HavenFSK::DspPipeline::txAudioReady,
            this, [this](const std::vector<float>& samples) {
                QString outDev = m_outputDevCombo->currentText();
                m_audio->startTx(outDev, samples);
            });

    // DspPipeline → MainWindow (decoded messages and state)
    connect(m_pipeline, &HavenFSK::DspPipeline::messageReceived,
            this, &MainWindow::onMessageReceived);
    connect(m_pipeline, &HavenFSK::DspPipeline::dcdChanged,
            this, &MainWindow::onDcdChanged);
    connect(m_pipeline, &HavenFSK::DspPipeline::rxStateChanged,
            this, &MainWindow::onRxStateChanged);

    // AudioEngine txComplete → DspPipeline (clear transmitting flag)
    connect(m_audio, &AudioEngine::txComplete,
            m_pipeline, &HavenFSK::DspPipeline::onTxComplete);
}

void MainWindow::startAudio() {
    QString inDev = m_inputDevCombo->currentText();
    if (!m_audio->startRx(inDev)) {
        m_statusLabel->setText("Audio input error — check device selection");
    } else {
        m_statusLabel->setText("Listening...");
    }
}

// ── Slots ─────────────────────────────────────────────────────────────────

void MainWindow::onTransmit() {
    QString text = m_txInput->text().trimmed();
    if (text.isEmpty()) return;

    if (m_audio->isTransmitting()) {
        m_statusLabel->setText("TX in progress — please wait");
        return;
    }

    // Stop RX while transmitting
    m_audio->stopRx();

    m_txButton->setEnabled(false);
    m_txInput->setEnabled(false);
    m_statusLabel->setText("Transmitting...");

    if (!m_pipeline->transmit(text)) {
        m_statusLabel->setText("TX failed");
        m_txButton->setEnabled(true);
        m_txInput->setEnabled(true);
        m_audio->startRx(m_inputDevCombo->currentText());
    }
}

void MainWindow::onTxComplete() {
    m_txButton->setEnabled(true);
    m_txInput->setEnabled(true);
    m_txInput->clear();
    m_statusLabel->setText("Listening...");

    // Resume RX after TX
    m_audio->startRx(m_inputDevCombo->currentText());
}

void MainWindow::onMessageReceived(const HavenFSK::RxMessage& msg) {
    QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss");
    QString line = QString("[%1] %2").arg(timestamp).arg(msg.text);
    if (!msg.crcOk)     line += " [CRC FAIL]";
    if (!msg.converged) line += " [FEC NC]";
    m_rxText->append(line);
    m_statusLabel->setText(
        QString("Message received — CRC %1, FEC %2")
            .arg(msg.crcOk ? "OK" : "FAIL")
            .arg(msg.converged ? "converged" : "not converged"));
}

void MainWindow::onDcdChanged(bool active) {
    m_dcdLabel->setText(active ? "DCD: ON" : "DCD: --");
    m_dcdLabel->setStyleSheet(
        active ? "color: green; font-weight: bold;" : "");
}

void MainWindow::onRxStateChanged(HavenFSK::RxState state) {
    switch (state) {
    case HavenFSK::RxState::Idle:
        m_rxStateLabel->setText("Idle");
        break;
    case HavenFSK::RxState::Searching:
        m_rxStateLabel->setText("Searching...");
        break;
    case HavenFSK::RxState::Receiving:
        m_rxStateLabel->setText("Receiving");
        break;
    }
}

void MainWindow::onAudioError(const QString& message) {
    m_statusLabel->setText("Audio error: " + message);
    QMessageBox::warning(this, "Audio Error", message);
}

void MainWindow::onRxLevelChanged(float level) {
    m_rxLevel->setValue(static_cast<int>(level * 100.0f));
}
