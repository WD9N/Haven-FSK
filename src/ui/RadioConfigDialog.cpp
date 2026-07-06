#include "RadioConfigDialog.h"
#include "../radio/RadioSettings.h"
#include "../radio/HamlibClient.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QPushButton>
#include <QSettings>
#include <QLabel>
#include <QSerialPortInfo>
#include <QCompleter>
#include <QScrollArea>
#include <QScreen>
#include <QGuiApplication>
#include <algorithm>

RadioConfigDialog::RadioConfigDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("Radio Control");
    // A bit wider than the content's natural minimum: when a vertical
    // scrollbar appears (switching to a taller method's group after the
    // dialog is already open, e.g. Direct/Hamlib), it steals ~20px from
    // the viewport -- without this margin that clipped the Refresh/Close
    // buttons at the right edge (found by actually switching methods and
    // screenshotting, not assumed).
    setMinimumWidth(470);
    setModal(true);
    setupUi();
    loadSettings();

    // Cap the dialog to the screen's available height so it can never
    // land taller than the display (small/laptop screens, or Windows
    // display scaling, could otherwise push the bottom -- Connect/Save/
    // Close -- off-screen with no way to reach it). onMethodChanged()
    // (called from loadSettings() above) already hid the two inactive
    // connection-method groups, so m_scrollContent->sizeHint() here
    // reflects only the fields actually relevant to the saved method --
    // the common case fits on-screen with no scrolling needed at all.
    // Note: this dialog's OWN sizeHint() can't be used for this --
    // QScrollArea deliberately does not propagate its content widget's
    // full size demand upward, so it comes back small/arbitrary once
    // setupUi() has wrapped everything in one.
    QScreen* screen = this->screen();
    if (!screen) screen = QGuiApplication::primaryScreen();
    int naturalHeight = m_scrollContent->sizeHint().height() + 40; // scroll/dialog chrome
    if (screen) {
        int maxHeight = static_cast<int>(screen->availableGeometry().height() * 0.9);
        setMaximumHeight(maxHeight);
        resize(width(), std::min(naturalHeight, maxHeight));
    } else {
        resize(width(), naturalHeight);
    }
}

void RadioConfigDialog::setupUi() {
    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);

    auto* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    // Left at the default (AsNeeded) rather than forced off: the width
    // margin above covers the normal case, but if some future field
    // combination is still too wide, scrolling beats silently clipping
    // controls off the visible edge.
    outerLayout->addWidget(scrollArea);

    auto* content = new QWidget;
    m_scrollContent = content;
    scrollArea->setWidget(content);
    auto* layout = new QVBoxLayout(content);

    // Method selector
    auto* methodGroup  = new QGroupBox("Radio Control Method");
    auto* methodLayout = new QVBoxLayout(methodGroup);

    m_radioNone    = new QRadioButton("None (audio only / VOX)");
    m_radioRigctld = new QRadioButton(
        "rigctld  —  Hamlib server (covers most radios)");
    m_radioTCI     = new QRadioButton(
        "TCI WebSocket  —  Thetis / ExpertSDR / HPSDR");
    m_radioHamlib  = new QRadioButton(
        "Direct / Hamlib  —  CAT over USB/serial, no external rigctld");
    m_radioHamlib->setEnabled(HamlibClient::isAvailable());
    if (!HamlibClient::isAvailable()) {
        m_radioHamlib->setToolTip(
            "Not available — this build was compiled without Hamlib "
            "support. See build.bat / HAMLIB_DIR.");
    }

    methodLayout->addWidget(m_radioNone);
    methodLayout->addWidget(m_radioRigctld);
    methodLayout->addWidget(m_radioTCI);
    methodLayout->addWidget(m_radioHamlib);

    auto* methodNote = new QLabel(
        "VOX: radio handles PTT via audio level — no rig control needed.\n"
        "rigctld: start Hamlib rigctld before connecting.\n"
        "TCI: enable TCI server in your SDR software first.\n"
        "Direct/Hamlib: connects straight to a USB/serial CAT port — no "
        "separate process to run.");
    methodNote->setStyleSheet("color: gray; font-size: 9pt;");
    methodNote->setWordWrap(true);
    methodLayout->addWidget(methodNote);
    layout->addWidget(methodGroup);

    // rigctld settings
    m_rigctldGroup = new QGroupBox("rigctld Connection");
    auto* rigForm  = new QFormLayout(m_rigctldGroup);
    m_rigctldHost  = new QLineEdit;
    m_rigctldHost->setPlaceholderText("localhost");
    rigForm->addRow("Host:", m_rigctldHost);
    m_rigctldPort  = new QSpinBox;
    m_rigctldPort->setRange(1, 65535);
    m_rigctldPort->setValue(4532);
    rigForm->addRow("Port:", m_rigctldPort);
    auto* rigNote  = new QLabel("Default port 4532.");
    rigNote->setStyleSheet("color: gray; font-size: 9pt;");
    rigForm->addRow("", rigNote);

    m_setModeOnConnect = new QCheckBox("Set radio to data mode on connect");
    rigForm->addRow("", m_setModeOnConnect);
    m_connectModeString = new QLineEdit;
    m_connectModeString->setPlaceholderText("PKTUSB");
    m_connectModeString->setToolTip(
        "Hamlib rigctld mode token sent on connect.\n"
        "Kenwood TS-590SG/TS-480HX data mode: PKTUSB / PKTLSB.\n"
        "Check your rig's Hamlib backend for the correct token.");
    rigForm->addRow("Mode on connect:", m_connectModeString);
    connect(m_setModeOnConnect, &QCheckBox::toggled,
            m_connectModeString, &QLineEdit::setEnabled);
    layout->addWidget(m_rigctldGroup);

    // TCI settings
    m_tciGroup    = new QGroupBox("TCI Connection");
    auto* tciForm = new QFormLayout(m_tciGroup);
    m_tciHost     = new QLineEdit;
    m_tciHost->setPlaceholderText("localhost");
    tciForm->addRow("Host:", m_tciHost);
    m_tciPort     = new QSpinBox;
    m_tciPort->setRange(1, 65535);
    m_tciPort->setValue(50001);
    tciForm->addRow("Port:", m_tciPort);
    auto* tciNote = new QLabel(
        "Default port 50001 (Thetis). Thetis HL2 users: verify port in Setup → TCI.");
    tciNote->setStyleSheet("color: gray; font-size: 9pt;");
    tciNote->setWordWrap(true);
    tciForm->addRow("", tciNote);
    layout->addWidget(m_tciGroup);

    // Direct Hamlib settings
    m_hamlibGroup = new QGroupBox("Direct / Hamlib Connection");
    auto* hamlibForm = new QFormLayout(m_hamlibGroup);

    m_hamlibRigModel = new QComboBox;
    m_hamlibRigModel->setEditable(true);
    m_hamlibRigModel->setInsertPolicy(QComboBox::NoInsert);
    for (const auto& rig : HamlibClient::availableRigs())
        m_hamlibRigModel->addItem(
            QString("%1 %2").arg(rig.mfgName, rig.modelName), rig.model);
    auto* rigCompleter = new QCompleter(m_hamlibRigModel->model(), m_hamlibRigModel);
    rigCompleter->setCaseSensitivity(Qt::CaseInsensitive);
    rigCompleter->setFilterMode(Qt::MatchContains);
    rigCompleter->setCompletionMode(QCompleter::PopupCompletion);
    m_hamlibRigModel->setCompleter(rigCompleter);
    hamlibForm->addRow("Rig model:", m_hamlibRigModel);

    auto* portRow = new QHBoxLayout;
    m_hamlibPort = new QComboBox;
    m_hamlibRefreshPorts = new QPushButton("Refresh");
    portRow->addWidget(m_hamlibPort, 1);
    portRow->addWidget(m_hamlibRefreshPorts);
    hamlibForm->addRow("Port:", portRow);
    connect(m_hamlibRefreshPorts, &QPushButton::clicked,
            this, &RadioConfigDialog::onRefreshHamlibPorts);

    m_hamlibBaud = new QComboBox;
    m_hamlibBaud->setEditable(true);
    for (int baud : {1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200})
        m_hamlibBaud->addItem(QString::number(baud));
    hamlibForm->addRow("Baud:", m_hamlibBaud);

    auto* hamlibNote = new QLabel(
        "Links Hamlib directly — no separate rigctld process to run. "
        "Type to search the rig model list (e.g. \"590\" or \"kenwood\").");
    hamlibNote->setStyleSheet("color: gray; font-size: 9pt;");
    hamlibNote->setWordWrap(true);
    hamlibForm->addRow("", hamlibNote);

    layout->addWidget(m_hamlibGroup);
    onRefreshHamlibPorts();

    // TX sequencing timing
    auto* timingGroup = new QGroupBox("TX Sequencing Timing");
    auto* timingForm  = new QFormLayout(timingGroup);

    m_pttLeadMs = new QSpinBox;
    m_pttLeadMs->setRange(0, 2000);
    m_pttLeadMs->setSuffix(" ms");
    m_pttLeadMs->setValue(150);
    m_pttLeadMs->setToolTip(
        "Delay from PTT assert to audio start.\n"
        "Allows radio time to switch from RX to TX.\n"
        "HL2/TCI: 50–150ms  Modern CAT: 100–200ms\n"
        "Older relay-switched radios: 200–500ms");
    timingForm->addRow("PTT Lead Time:", m_pttLeadMs);

    m_txTailMs = new QSpinBox;
    m_txTailMs->setRange(0, 2000);
    m_txTailMs->setSuffix(" ms");
    m_txTailMs->setValue(200);
    m_txTailMs->setToolTip(
        "Delay from audio end to PTT release.\n"
        "Allows audio hardware buffer to drain\n"
        "and last audio to fully transmit.\n"
        "Recommended: 150–300ms");
    timingForm->addRow("TX Tail Time:", m_txTailMs);

    auto* timingNote = new QLabel(
        "Total TX hold = PTT lead + audio + TX tail.\n"
        "Increase PTT lead if audio starts before radio is in TX.\n"
        "Increase TX tail if last part of audio is cut off.");
    timingNote->setStyleSheet("color: gray; font-size: 8pt;");
    timingNote->setWordWrap(true);
    timingForm->addRow("", timingNote);

    layout->addWidget(timingGroup);

    // Button row: [Connect Rig] [Disconnect Rig] <stretch> [Save] [Close]
    auto* btnLayout = new QHBoxLayout;

    m_connectBtn = new QPushButton("Connect Rig");
    m_connectBtn->setStyleSheet(
        "QPushButton { background: #1a3a1a; color: #88cc88; "
        "border: 1px solid #336633; padding: 4px 12px; }"
        "QPushButton:hover { background: #2a5a2a; }"
        "QPushButton:disabled { background: #111; color: #444; "
        "border: 1px solid #222; }");

    m_disconnectBtn = new QPushButton("Disconnect Rig");
    m_disconnectBtn->setStyleSheet(
        "QPushButton { background: #3a1a1a; color: #cc8888; "
        "border: 1px solid #663333; padding: 4px 12px; }"
        "QPushButton:hover { background: #5a2a2a; }"
        "QPushButton:disabled { background: #111; color: #444; "
        "border: 1px solid #222; }");
    m_disconnectBtn->setEnabled(false);

    auto* saveBtn  = new QPushButton("Save");
    auto* closeBtn = new QPushButton("Close");

    btnLayout->addWidget(m_connectBtn);
    btnLayout->addWidget(m_disconnectBtn);
    btnLayout->addStretch();
    btnLayout->addWidget(saveBtn);
    btnLayout->addWidget(closeBtn);

    layout->addLayout(btnLayout);

    m_connectStatusLabel = new QLabel;
    m_connectStatusLabel->setWordWrap(true);
    m_connectStatusLabel->setStyleSheet("color: gray;");
    layout->addWidget(m_connectStatusLabel);

    connect(m_connectBtn,    &QPushButton::clicked,
            this, &RadioConfigDialog::onConnect);
    connect(m_disconnectBtn, &QPushButton::clicked,
            this, &RadioConfigDialog::onDisconnect);
    connect(saveBtn,  &QPushButton::clicked,
            this, &RadioConfigDialog::onSave);
    connect(closeBtn, &QPushButton::clicked,
            this, &QDialog::accept);

    connect(m_radioNone,    &QRadioButton::toggled,
            this, &RadioConfigDialog::onMethodChanged);
    connect(m_radioRigctld, &QRadioButton::toggled,
            this, &RadioConfigDialog::onMethodChanged);
    connect(m_radioTCI,     &QRadioButton::toggled,
            this, &RadioConfigDialog::onMethodChanged);
    connect(m_radioHamlib,  &QRadioButton::toggled,
            this, &RadioConfigDialog::onMethodChanged);
}

void RadioConfigDialog::onRefreshHamlibPorts() {
    QString previous = m_hamlibPort->currentData().toString();
    m_hamlibPort->clear();
    for (const auto& info : QSerialPortInfo::availablePorts()) {
        QString label = info.portName();
        if (!info.description().isEmpty())
            label += QString(" (%1)").arg(info.description());
        m_hamlibPort->addItem(label, info.portName());
    }
    int idx = m_hamlibPort->findData(previous);
    if (idx >= 0) m_hamlibPort->setCurrentIndex(idx);
}

void RadioConfigDialog::loadSettings() {
    QSettings s;
    QString method = s.value(
        HavenFSK::RadioSettingsKeys::RIG_METHOD, "none").toString();
    if      (method == "rigctld") m_radioRigctld->setChecked(true);
    else if (method == "tci")     m_radioTCI->setChecked(true);
    else if (method == "hamlib" && HamlibClient::isAvailable())
                                   m_radioHamlib->setChecked(true);
    else                          m_radioNone->setChecked(true);

    m_rigctldHost->setText(HavenFSK::rigctldHost());
    m_rigctldPort->setValue(HavenFSK::rigctldPort());
    m_tciHost->setText(HavenFSK::tciHost());
    m_tciPort->setValue(HavenFSK::tciPort());

    int rigIdx = m_hamlibRigModel->findData(HavenFSK::hamlibRigModel());
    if (rigIdx >= 0) m_hamlibRigModel->setCurrentIndex(rigIdx);
    int portIdx = m_hamlibPort->findData(HavenFSK::hamlibPort());
    if (portIdx >= 0) m_hamlibPort->setCurrentIndex(portIdx);
    m_hamlibBaud->setCurrentText(QString::number(HavenFSK::hamlibBaud()));

    m_pttLeadMs->setValue(HavenFSK::pttLeadMs());
    m_txTailMs->setValue(HavenFSK::txTailMs());
    m_setModeOnConnect->setChecked(HavenFSK::setModeOnConnect());
    m_connectModeString->setText(HavenFSK::connectModeString());
    m_connectModeString->setEnabled(m_setModeOnConnect->isChecked());
    onMethodChanged();
}

void RadioConfigDialog::saveSettings() {
    QSettings s;
    QString method = "none";
    if      (m_radioRigctld->isChecked()) method = "rigctld";
    else if (m_radioTCI->isChecked())     method = "tci";
    else if (m_radioHamlib->isChecked())  method = "hamlib";
    s.setValue(HavenFSK::RadioSettingsKeys::RIG_METHOD, method);
    s.setValue(HavenFSK::RadioSettingsKeys::RIGCTLD_HOST,
               m_rigctldHost->text().trimmed());
    s.setValue(HavenFSK::RadioSettingsKeys::RIGCTLD_PORT,
               m_rigctldPort->value());
    s.setValue(HavenFSK::RadioSettingsKeys::TCI_HOST,
               m_tciHost->text().trimmed());
    s.setValue(HavenFSK::RadioSettingsKeys::TCI_PORT,
               m_tciPort->value());
    s.setValue(HavenFSK::RadioSettingsKeys::HAMLIB_RIG_MODEL,
               m_hamlibRigModel->currentData().toInt());
    s.setValue(HavenFSK::RadioSettingsKeys::HAMLIB_PORT,
               m_hamlibPort->currentData().toString());
    s.setValue(HavenFSK::RadioSettingsKeys::HAMLIB_BAUD,
               m_hamlibBaud->currentText().toInt());
    s.setValue(HavenFSK::RadioSettingsKeys::PTT_LEAD_MS,
               m_pttLeadMs->value());
    s.setValue(HavenFSK::RadioSettingsKeys::TX_TAIL_MS,
               m_txTailMs->value());
    s.setValue(HavenFSK::RadioSettingsKeys::SET_MODE_ON_CONNECT,
               m_setModeOnConnect->isChecked());
    s.setValue(HavenFSK::RadioSettingsKeys::CONNECT_MODE_STRING,
               m_connectModeString->text().trimmed());
}

void RadioConfigDialog::setConnected(bool connected) {
    m_isConnected = connected;
    if (m_connectBtn)    m_connectBtn->setEnabled(!connected);
    if (m_disconnectBtn) m_disconnectBtn->setEnabled(connected);
}

void RadioConfigDialog::onMethodChanged() {
    // Hidden, not just disabled -- only one method's settings are ever
    // relevant at a time, and hiding the other two reclaims real
    // vertical space (see the constructor's screen-height cap: this is
    // what keeps the dialog from needing to scroll in the common case).
    m_rigctldGroup->setVisible(m_radioRigctld->isChecked());
    m_tciGroup->setVisible(m_radioTCI->isChecked());
    m_hamlibGroup->setVisible(m_radioHamlib->isChecked());
}

void RadioConfigDialog::onConnect() {
    saveSettings();
    emit configChanged();
    emit connectRequested();
    // Connecting is async (and, for Hamlib, may take a few bounded
    // background retries) — don't claim success yet. onConnectSucceeded()/
    // onConnectFailed() (wired to the live RadioInterface by
    // MainWindow::onOpenRadioConfig()) update the buttons/label once the
    // real outcome is known.
    m_connectBtn->setEnabled(false);
    m_disconnectBtn->setEnabled(true);
    m_connectStatusLabel->setStyleSheet("color: gray;");
    m_connectStatusLabel->setText("Connecting…");
}

void RadioConfigDialog::onDisconnect() {
    emit disconnectRequested();
    m_connectBtn->setEnabled(true);
    m_disconnectBtn->setEnabled(false);
    m_connectStatusLabel->clear();
}

void RadioConfigDialog::onConnectSucceeded() {
    setConnected(true);
    m_connectStatusLabel->setStyleSheet("color: #88cc88;");
    m_connectStatusLabel->setText("Connected.");
}

void RadioConfigDialog::onConnectFailed(const QString& reason) {
    // Gave up retrying (see RadioInterface::connectFailed) — return the
    // buttons to "not connected" so the operator can fix settings (e.g.
    // COM port) and hit Connect again, rather than being stuck looking
    // "still trying" indefinitely.
    setConnected(false);
    m_connectStatusLabel->setStyleSheet("color: #cc8888;");
    m_connectStatusLabel->setText(reason);
}

void RadioConfigDialog::onSave() {
    saveSettings();
    emit configChanged();
}
