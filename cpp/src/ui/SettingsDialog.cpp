#include "SettingsDialog.h"
#include "../radio/RadioSettings.h"
#include "../audio/AudioSettings.h"
#include "../audio/AudioEngine.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QSettings>
#include <QGroupBox>
#include <QLabel>
#include <QScrollArea>

SettingsDialog::SettingsDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("HAVEN-FSK Settings");
    setMinimumWidth(500);
    setMinimumHeight(520);
    setModal(true);

    m_tabs = new QTabWidget(this);
    setupStationTab();
    setupRadioTab();
    setupAudioTab();

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok |
        QDialogButtonBox::Apply |
        QDialogButtonBox::Cancel, this);

    connect(buttons, &QDialogButtonBox::accepted,
            this, &SettingsDialog::onOk);
    connect(buttons->button(QDialogButtonBox::Apply),
            &QPushButton::clicked,
            this, &SettingsDialog::onApply);
    connect(buttons, &QDialogButtonBox::rejected,
            this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_tabs);
    layout->addWidget(buttons);

    loadSettings();
}

// ── Station Information tab ───────────────────────────────────────────────

void SettingsDialog::setupStationTab() {
    auto* widget = new QWidget;
    auto* layout = new QVBoxLayout(widget);

    // Identity group
    auto* idGroup = new QGroupBox("Operator Identity");
    auto* idForm  = new QFormLayout(idGroup);

    m_callsign = new QLineEdit;
    m_callsign->setPlaceholderText("e.g. WD9N");
    m_callsign->setMaxLength(12);
    idForm->addRow("Callsign *:", m_callsign);

    m_callWarning = new QLabel(
        "⚠  Callsign required — TX blocked until set");
    m_callWarning->setStyleSheet("color: red;");
    m_callWarning->setVisible(false);
    idForm->addRow("", m_callWarning);

    m_grid = new QLineEdit;
    m_grid->setPlaceholderText("e.g. DN31");
    m_grid->setMaxLength(6);
    idForm->addRow("Grid Square:", m_grid);

    m_opName = new QLineEdit;
    m_opName->setPlaceholderText("e.g. John");
    idForm->addRow("Operator Name:", m_opName);

    layout->addWidget(idGroup);

    // POTA group — dynamic unbounded list
    auto* potaGroup  = new QGroupBox(
        "POTA References (leave empty if not activating)");
    auto* potaLayout = new QVBoxLayout(potaGroup);

    auto* potaNote = new QLabel(
        "Add all park references for your activation location.\n"
        "No limit — boundary overlaps may involve many parks.");
    potaNote->setStyleSheet("color: gray; font-size: 9pt;");
    potaLayout->addWidget(potaNote);

    m_potaList = new QListWidget;
    m_potaList->setMaximumHeight(120);
    m_potaList->setAlternatingRowColors(true);
    potaLayout->addWidget(m_potaList);

    // Entry row
    auto* potaEntryRow = new QHBoxLayout;
    m_potaEntry = new QLineEdit;
    m_potaEntry->setPlaceholderText("e.g. K-1234");
    m_potaEntry->setMaxLength(20);
    m_potaAdd    = new QPushButton("+ Add");
    m_potaRemove = new QPushButton("Remove");
    m_potaRemove->setEnabled(false);
    potaEntryRow->addWidget(m_potaEntry, 1);
    potaEntryRow->addWidget(m_potaAdd);
    potaEntryRow->addWidget(m_potaRemove);
    potaLayout->addLayout(potaEntryRow);

    auto* activatorNote = new QLabel(
        "Setting any POTA reference enables Activator mode "
        "(TX backoff 0–50ms).");
    activatorNote->setStyleSheet("color: gray; font-size: 9pt;");
    activatorNote->setWordWrap(true);
    potaLayout->addWidget(activatorNote);

    layout->addWidget(potaGroup);

    // SOTA group
    auto* sotaGroup = new QGroupBox(
        "SOTA Reference (leave empty if not activating)");
    auto* sotaForm  = new QFormLayout(sotaGroup);
    m_sotaRef = new QLineEdit;
    m_sotaRef->setPlaceholderText("e.g. W7W/SE-001");
    sotaForm->addRow("Summit:", m_sotaRef);
    layout->addWidget(sotaGroup);

    // Field Day group
    auto* fdGroup  = new QGroupBox(
        "Field Day (leave empty if not operating FD)");
    auto* fdLayout = new QHBoxLayout(fdGroup);
    m_fdClass = new QLineEdit;
    m_fdClass->setPlaceholderText("e.g. 2A");
    m_fdClass->setMaxLength(4);
    m_fdClass->setMaximumWidth(80);
    m_fdSection = new QLineEdit;
    m_fdSection->setPlaceholderText("e.g. MWA");
    m_fdSection->setMaxLength(5);
    m_fdSection->setMaximumWidth(80);
    fdLayout->addWidget(new QLabel("Class:"));
    fdLayout->addWidget(m_fdClass);
    fdLayout->addWidget(new QLabel("Section:"));
    fdLayout->addWidget(m_fdSection);
    fdLayout->addStretch();
    layout->addWidget(fdGroup);
    layout->addStretch();

    m_tabs->addTab(widget, "Station Info");

    // Connections
    connect(m_callsign, &QLineEdit::textChanged,
            this, [this](const QString& t) {
                m_callWarning->setVisible(t.trimmed().isEmpty());
            });

    connect(m_potaAdd, &QPushButton::clicked,
            this, &SettingsDialog::onAddPotaRef);

    connect(m_potaEntry, &QLineEdit::returnPressed,
            this, &SettingsDialog::onAddPotaRef);

    connect(m_potaRemove, &QPushButton::clicked,
            this, &SettingsDialog::onRemovePotaRef);

    connect(m_potaList, &QListWidget::itemSelectionChanged,
            this, [this]() {
                m_potaRemove->setEnabled(
                    !m_potaList->selectedItems().isEmpty());
            });
}

void SettingsDialog::onAddPotaRef() {
    QString ref = m_potaEntry->text().trimmed().toUpper();
    if (ref.isEmpty()) return;

    for (int i = 0; i < m_potaList->count(); i++) {
        if (m_potaList->item(i)->text() == ref) {
            m_potaEntry->clear();
            return;
        }
    }

    m_potaList->addItem(ref);
    m_potaEntry->clear();
    m_potaEntry->setFocus();
}

void SettingsDialog::onRemovePotaRef() {
    auto selected = m_potaList->selectedItems();
    for (auto* item : selected)
        delete m_potaList->takeItem(m_potaList->row(item));
    m_potaRemove->setEnabled(false);
}

// ── Radio Control tab ─────────────────────────────────────────────────────

void SettingsDialog::setupRadioTab() {
    auto* widget = new QWidget;
    auto* layout = new QVBoxLayout(widget);

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

    // rigctld
    m_rigctldGroup = new QGroupBox("rigctld Connection");
    auto* rigForm  = new QFormLayout(m_rigctldGroup);
    m_rigctldHost  = new QLineEdit;
    m_rigctldHost->setPlaceholderText("localhost");
    rigForm->addRow("Host:", m_rigctldHost);
    m_rigctldPort = new QSpinBox;
    m_rigctldPort->setRange(1, 65535);
    m_rigctldPort->setValue(4532);
    rigForm->addRow("Port:", m_rigctldPort);
    auto* rigNote = new QLabel(
        "Default port 4532 (rigctld universal default).");
    rigNote->setStyleSheet("color: gray; font-size: 9pt;");
    rigForm->addRow("", rigNote);
    layout->addWidget(m_rigctldGroup);

    // TCI
    m_tciGroup    = new QGroupBox("TCI Connection");
    auto* tciForm = new QFormLayout(m_tciGroup);
    m_tciHost     = new QLineEdit;
    m_tciHost->setPlaceholderText("localhost");
    tciForm->addRow("Host:", m_tciHost);
    m_tciPort = new QSpinBox;
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

    m_tabs->addTab(widget, "Radio Control");

    connect(m_radioNone,    &QRadioButton::toggled,
            this, &SettingsDialog::onRadioMethodChanged);
    connect(m_radioRigctld, &QRadioButton::toggled,
            this, &SettingsDialog::onRadioMethodChanged);
    connect(m_radioTCI,     &QRadioButton::toggled,
            this, &SettingsDialog::onRadioMethodChanged);
}

void SettingsDialog::onRadioMethodChanged() {
    m_rigctldGroup->setEnabled(m_radioRigctld->isChecked());
    m_tciGroup->setEnabled(m_radioTCI->isChecked());
}

// ── Audio Devices tab ─────────────────────────────────────────────────────

void SettingsDialog::setupAudioTab() {
    auto* widget = new QWidget;
    auto* layout = new QFormLayout(widget);

    m_inputDev = new QComboBox;
    m_inputDev->addItems(AudioEngine::availableInputDevices());
    layout->addRow("Input Device:", m_inputDev);

    m_outputDev = new QComboBox;
    m_outputDev->addItems(AudioEngine::availableOutputDevices());
    layout->addRow("Output Device:", m_outputDev);

    auto* note = new QLabel(
        "HAVEN-FSK operates at 48000 Hz mono (fixed by specification).\n"
        "Select your radio's USB audio interface or virtual audio cable.\n"
        "Audio restarts automatically when you click OK or Apply.");
    note->setStyleSheet("color: gray; font-size: 9pt;");
    note->setWordWrap(true);
    layout->addRow("", note);

    m_tabs->addTab(widget, "Audio");
}

// ── Load / Save ───────────────────────────────────────────────────────────

void SettingsDialog::loadSettings() {
    HavenFSK::StationInfo info = HavenFSK::loadStationInfo();

    m_callsign->setText(info.callsign);
    m_callWarning->setVisible(info.callsign.isEmpty());
    m_grid->setText(info.grid);
    m_opName->setText(info.opName);

    m_potaList->clear();
    for (const QString& ref : info.potaRefs)
        if (!ref.isEmpty())
            m_potaList->addItem(ref);

    m_sotaRef->setText(info.sotaRef);
    m_fdClass->setText(info.fdClass);
    m_fdSection->setText(info.fdSection);

    // Radio
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
    onRadioMethodChanged();

    // Audio
    int idxIn = m_inputDev->findText(HavenFSK::savedInputDevice());
    if (idxIn >= 0) m_inputDev->setCurrentIndex(idxIn);
    int idxOut = m_outputDev->findText(HavenFSK::savedOutputDevice());
    if (idxOut >= 0) m_outputDev->setCurrentIndex(idxOut);
}

void SettingsDialog::saveSettings() {
    HavenFSK::StationInfo info;
    info.callsign  = m_callsign->text().trimmed().toUpper();
    info.grid      = m_grid->text().trimmed().toUpper();
    info.opName    = m_opName->text().trimmed();
    info.sotaRef   = m_sotaRef->text().trimmed().toUpper();
    info.fdClass   = m_fdClass->text().trimmed().toUpper();
    info.fdSection = m_fdSection->text().trimmed().toUpper();

    for (int i = 0; i < m_potaList->count(); i++) {
        QString ref = m_potaList->item(i)->text().trimmed().toUpper();
        if (!ref.isEmpty()) info.potaRefs.append(ref);
    }

    HavenFSK::saveStationInfo(info);

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

    HavenFSK::saveInputDevice(m_inputDev->currentText());
    HavenFSK::saveOutputDevice(m_outputDev->currentText());
}

void SettingsDialog::onOk() {
    saveSettings();
    emit settingsChanged();
    accept();
}

void SettingsDialog::onApply() {
    saveSettings();
    emit settingsChanged();
}
