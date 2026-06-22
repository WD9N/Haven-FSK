#include "SettingsDialog.h"
#include "../radio/RadioSettings.h"
#include "../audio/AudioSettings.h"
#include "../audio/AudioEngine.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QSettings>
#include <QGroupBox>
#include <QLabel>
#include <QRegularExpression>

// Auto-correct POTA ref to canonical XX-NNNN format
static QString fixPotaRef(const QString& raw) {
    QString s = raw.trimmed().toUpper();
    static QRegularExpression correct("^[A-Z]{2}-[0-9]+$");
    if (correct.match(s).hasMatch()) return s;

    // Insert hyphen after first 2 letters: "US1234" → "US-1234"
    static QRegularExpression noHyphen("^([A-Z]{2})([0-9]+)$");
    auto m = noHyphen.match(s);
    if (m.hasMatch())
        return m.captured(1) + "-" + m.captured(2);

    return s;
}

SettingsDialog::SettingsDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("HAVEN-FSK Settings");
    setMinimumWidth(500);
    setMinimumHeight(460);
    setModal(true);

    m_tabs = new QTabWidget(this);
    setupStationTab();
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

    // Force-uppercase helper for identity fields
    auto forceUpper = [](QLineEdit* edit) {
        QObject::connect(edit, &QLineEdit::textEdited,
                         edit, [edit](const QString& text) {
                             int pos = edit->cursorPosition();
                             edit->setText(text.toUpper());
                             edit->setCursorPosition(pos);
                         });
    };

    // Identity group
    auto* idGroup = new QGroupBox("Operator Identity");
    auto* idForm  = new QFormLayout(idGroup);

    m_callsign = new QLineEdit;
    m_callsign->setPlaceholderText("Your callsign");  // Fix 5
    m_callsign->setMaxLength(12);
    forceUpper(m_callsign);  // Fix 4
    idForm->addRow("Callsign *:", m_callsign);

    m_callWarning = new QLabel(
        "⚠  Callsign required — TX blocked until set");
    m_callWarning->setStyleSheet("color: red;");
    m_callWarning->setVisible(false);
    idForm->addRow("", m_callWarning);

    m_grid = new QLineEdit;
    m_grid->setPlaceholderText("e.g. DN31");
    m_grid->setMaxLength(6);
    forceUpper(m_grid);  // Fix 4
    idForm->addRow("Grid Square:", m_grid);

    m_opName = new QLineEdit;
    m_opName->setPlaceholderText("e.g. John");
    idForm->addRow("Operator Name:", m_opName);

    m_state = new QLineEdit;
    m_state->setPlaceholderText("e.g. Illinois");
    idForm->addRow("State/Province:", m_state);

    m_county = new QLineEdit;
    m_county->setPlaceholderText("e.g. Cook");
    idForm->addRow("County:", m_county);

    layout->addWidget(idGroup);

    // POTA group — dynamic unbounded list
    auto* potaGroup  = new QGroupBox(
        "POTA References (leave empty if not activating)");
    auto* potaLayout = new QVBoxLayout(potaGroup);

    auto* potaNote = new QLabel(
        "Add all park references for your activation location.\n"
        "No limit — boundary overlaps may involve many parks.\n"
        "Format: XX-NNNN (e.g. US-1234, CA-0001)");
    potaNote->setStyleSheet("color: gray; font-size: 9pt;");
    potaLayout->addWidget(potaNote);

    m_potaList = new QListWidget;
    m_potaList->setMaximumHeight(120);
    m_potaList->setAlternatingRowColors(true);
    potaLayout->addWidget(m_potaList);

    auto* potaEntryRow = new QHBoxLayout;
    m_potaEntry = new QLineEdit;
    m_potaEntry->setPlaceholderText("XX-NNNN (e.g. US-1234)");
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
    forceUpper(m_sotaRef);  // Fix 4
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
    forceUpper(m_fdClass);  // Fix 4
    m_fdSection = new QLineEdit;
    m_fdSection->setPlaceholderText("e.g. MWA");
    m_fdSection->setMaxLength(5);
    m_fdSection->setMaximumWidth(80);
    forceUpper(m_fdSection);  // Fix 4
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
    // Fix 3: auto-correct format before adding
    QString ref = fixPotaRef(m_potaEntry->text());
    if (ref.isEmpty()) return;

    // Duplicate check
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

    m_state->setText(info.state);
    m_county->setText(info.county);
    m_sotaRef->setText(info.sotaRef);
    m_fdClass->setText(info.fdClass);
    m_fdSection->setText(info.fdSection);

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
    info.state     = m_state->text().trimmed();
    info.county    = m_county->text().trimmed();
    info.sotaRef   = m_sotaRef->text().trimmed().toUpper();
    info.fdClass   = m_fdClass->text().trimmed().toUpper();
    info.fdSection = m_fdSection->text().trimmed().toUpper();

    for (int i = 0; i < m_potaList->count(); i++) {
        QString ref = m_potaList->item(i)->text().trimmed().toUpper();
        if (!ref.isEmpty()) info.potaRefs.append(ref);
    }

    HavenFSK::saveStationInfo(info);
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
