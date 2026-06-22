#include "RadioConfigDialog.h"
#include "../radio/RadioSettings.h"
#include <QVBoxLayout>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QSettings>
#include <QLabel>

RadioConfigDialog::RadioConfigDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("Radio Control Configuration");
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
        "rigctld: start Hamlib rigctld before launching HAVEN-FSK.\n"
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
    auto* rigNote  = new QLabel("Default port 4532 (rigctld universal default).");
    rigNote->setStyleSheet("color: gray; font-size: 9pt;");
    rigForm->addRow("", rigNote);
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
        "Default port 50001 (standard Thetis).\n"
        "Thetis HL2 users: verify port in Setup → TCI.\n"
        "Compatible with all TCI protocol versions.");
    tciNote->setStyleSheet("color: gray; font-size: 9pt;");
    tciNote->setWordWrap(true);
    tciForm->addRow("", tciNote);
    layout->addWidget(m_tciGroup);
    layout->addStretch();

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok |
        QDialogButtonBox::Apply |
        QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted,
            this, &RadioConfigDialog::onOk);
    connect(buttons->button(QDialogButtonBox::Apply),
            &QPushButton::clicked,
            this, &RadioConfigDialog::onApply);
    connect(buttons, &QDialogButtonBox::rejected,
            this, &QDialog::reject);
    layout->addWidget(buttons);

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
}

void RadioConfigDialog::onMethodChanged() {
    m_rigctldGroup->setEnabled(m_radioRigctld->isChecked());
    m_tciGroup->setEnabled(m_radioTCI->isChecked());
}

void RadioConfigDialog::onOk() {
    saveSettings();
    emit configChanged();
    accept();
}

void RadioConfigDialog::onApply() {
    saveSettings();
    emit configChanged();
}
