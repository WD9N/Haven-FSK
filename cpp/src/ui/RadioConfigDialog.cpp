#include "RadioConfigDialog.h"
#include "../radio/RadioSettings.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QPushButton>
#include <QSettings>
#include <QLabel>

RadioConfigDialog::RadioConfigDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("Radio Control");
    setMinimumWidth(440);
    setModal(true);
    setupUi();
    loadSettings();
}

void RadioConfigDialog::setupUi() {
    auto* layout = new QVBoxLayout(this);

    // Method selector
    auto* methodGroup  = new QGroupBox("Radio Control Method");
    auto* methodLayout = new QVBoxLayout(methodGroup);

    m_radioNone    = new QRadioButton("None (audio only / VOX)");
    m_radioRigctld = new QRadioButton(
        "rigctld  —  Hamlib server (covers most radios)");
    m_radioTCI     = new QRadioButton(
        "TCI WebSocket  —  Thetis / ExpertSDR / HPSDR");

    methodLayout->addWidget(m_radioNone);
    methodLayout->addWidget(m_radioRigctld);
    methodLayout->addWidget(m_radioTCI);

    auto* methodNote = new QLabel(
        "VOX: radio handles PTT via audio level — no rig control needed.\n"
        "rigctld: start Hamlib rigctld before connecting.\n"
        "TCI: enable TCI server in your SDR software first.");
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
}

void RadioConfigDialog::loadSettings() {
    QSettings s;
    QString method = s.value(
        HavenFSK::RadioSettingsKeys::RIG_METHOD, "none").toString();
    if      (method == "rigctld") m_radioRigctld->setChecked(true);
    else if (method == "tci")     m_radioTCI->setChecked(true);
    else                          m_radioNone->setChecked(true);

    m_rigctldHost->setText(HavenFSK::rigctldHost());
    m_rigctldPort->setValue(HavenFSK::rigctldPort());
    m_tciHost->setText(HavenFSK::tciHost());
    m_tciPort->setValue(HavenFSK::tciPort());
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
    s.setValue(HavenFSK::RadioSettingsKeys::RIG_METHOD, method);
    s.setValue(HavenFSK::RadioSettingsKeys::RIGCTLD_HOST,
               m_rigctldHost->text().trimmed());
    s.setValue(HavenFSK::RadioSettingsKeys::RIGCTLD_PORT,
               m_rigctldPort->value());
    s.setValue(HavenFSK::RadioSettingsKeys::TCI_HOST,
               m_tciHost->text().trimmed());
    s.setValue(HavenFSK::RadioSettingsKeys::TCI_PORT,
               m_tciPort->value());
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
    m_rigctldGroup->setEnabled(m_radioRigctld->isChecked());
    m_tciGroup->setEnabled(m_radioTCI->isChecked());
}

void RadioConfigDialog::onConnect() {
    saveSettings();
    emit configChanged();
    emit connectRequested();
    m_connectBtn->setEnabled(false);
    m_disconnectBtn->setEnabled(true);
}

void RadioConfigDialog::onDisconnect() {
    emit disconnectRequested();
    m_connectBtn->setEnabled(true);
    m_disconnectBtn->setEnabled(false);
}

void RadioConfigDialog::onSave() {
    saveSettings();
    emit configChanged();
}
