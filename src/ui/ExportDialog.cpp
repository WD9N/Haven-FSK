#include "ExportDialog.h"
#include "../log/LogManager.h"
#include "../log/AdifExporter.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QStandardPaths>
#include <QSettings>
#include <QDateTime>
#include <QDate>

ExportDialog::ExportDialog(LogManager* log, QWidget* parent)
    : QDialog(parent)
    , m_log(log)
{
    setWindowTitle("Export Log");
    setMinimumWidth(420);
    setModal(true);

    auto* layout = new QVBoxLayout(this);

    // Date selection
    auto* dateGroup  = new QGroupBox("Export Date");
    auto* dateLayout = new QVBoxLayout(dateGroup);

    QString todayUtc = QDateTime::currentDateTimeUtc().toString("yyyy-MM-dd");
    m_currentDay = new QRadioButton(
        QString("Current UTC day  (%1)").arg(todayUtc));
    m_currentDay->setChecked(true);
    m_selectDay = new QRadioButton("Select date:");

    auto* dateRow = new QHBoxLayout;
    m_datePicker  = new QDateEdit(QDate::currentDate());
    m_datePicker->setDisplayFormat("yyyy-MM-dd");
    m_datePicker->setCalendarPopup(true);
    m_datePicker->setEnabled(false);
    dateRow->addWidget(m_selectDay);
    dateRow->addWidget(m_datePicker);
    dateRow->addStretch();

    dateLayout->addWidget(m_currentDay);
    dateLayout->addLayout(dateRow);
    layout->addWidget(dateGroup);

    // Export folder
    auto* folderGroup  = new QGroupBox("Export Folder");
    auto* folderLayout = new QHBoxLayout(folderGroup);
    m_exportPath = new QLineEdit(defaultExportPath());
    m_browseBtn  = new QPushButton("...");
    m_browseBtn->setMaximumWidth(30);
    folderLayout->addWidget(m_exportPath, 1);
    folderLayout->addWidget(m_browseBtn);
    layout->addWidget(folderGroup);

    // Preview
    auto* previewGroup  = new QGroupBox("Files to be generated");
    auto* previewLayout = new QVBoxLayout(previewGroup);
    m_preview = new QLabel("Select a date to see preview");
    m_preview->setStyleSheet("font-family: 'Courier New'; font-size: 9pt;");
    m_preview->setWordWrap(true);
    m_preview->setMinimumHeight(60);
    previewLayout->addWidget(m_preview);
    layout->addWidget(previewGroup);

    // Buttons
    auto* buttons = new QDialogButtonBox(Qt::Horizontal);
    m_exportBtn   = new QPushButton("Export");
    auto* cancelBtn = new QPushButton("Cancel");
    buttons->addButton(m_exportBtn,  QDialogButtonBox::AcceptRole);
    buttons->addButton(cancelBtn,    QDialogButtonBox::RejectRole);
    layout->addWidget(buttons);

    connect(m_currentDay, &QRadioButton::toggled,
            this, &ExportDialog::onDateOptionChanged);
    connect(m_selectDay,  &QRadioButton::toggled,
            this, &ExportDialog::onDateOptionChanged);
    connect(m_datePicker, &QDateEdit::dateChanged,
            this, [this]() { refreshPreview(); });
    connect(m_browseBtn,  &QPushButton::clicked,
            this, &ExportDialog::onBrowse);
    connect(m_exportBtn,  &QPushButton::clicked,
            this, &ExportDialog::onExport);
    connect(cancelBtn,    &QPushButton::clicked,
            this, &QDialog::reject);

    refreshPreview();
}

QString ExportDialog::selectedDateUtc() const {
    if (m_currentDay->isChecked())
        return QDateTime::currentDateTimeUtc().toString("yyyyMMdd");
    return m_datePicker->date().toString("yyyyMMdd");
}

QString ExportDialog::defaultExportPath() const {
    QSettings s;
    QString saved = s.value("export/path").toString();
    if (!saved.isEmpty()) return saved;
    return QStandardPaths::writableLocation(
               QStandardPaths::DocumentsLocation) + "/HAVEN-FSK";
}

void ExportDialog::onDateOptionChanged() {
    m_datePicker->setEnabled(m_selectDay->isChecked());
    refreshPreview();
}

void ExportDialog::refreshPreview() {
    QString dateUtc     = selectedDateUtc();
    QString displayDate = QDate::fromString(dateUtc, "yyyyMMdd")
                              .toString("yyyy-MM-dd");

    auto contacts = m_log->contactsForDate(dateUtc);
    if (contacts.isEmpty()) {
        m_preview->setText(
            QString("No contacts logged on %1").arg(displayDate));
        m_exportBtn->setEnabled(false);
        return;
    }

    m_exportBtn->setEnabled(true);

    QString myCall = contacts.first()["my_callsign"].toString().toUpper();
    if (myCall.isEmpty()) myCall = "NOCALL";

    int count = contacts.size();
    QStringList lines;

    QStringList potaRefs = m_log->potaRefsForDate(dateUtc);
    for (const QString& ref : potaRefs)
        lines << QString("  %1@%2-%3.adi  (%4 QSOs)")
                 .arg(myCall, ref, dateUtc).arg(count);

    if (potaRefs.isEmpty()) {
        QStringList sotaRefs = m_log->sotaRefsForDate(dateUtc);
        for (const QString& ref : sotaRefs) {
            QString sanitized = ref;
            sanitized.replace('/', '-');
            lines << QString("  %1-%2-%3.adi  (%4 QSOs)")
                     .arg(myCall, sanitized, dateUtc).arg(count);
        }
    }

    lines << QString("  %1-%2.adi  (%3 QSOs, general)")
             .arg(myCall, dateUtc).arg(count);

    m_preview->setText(lines.join("\n"));
}

void ExportDialog::onBrowse() {
    QString dir = QFileDialog::getExistingDirectory(
        this, "Select Export Folder", m_exportPath->text());
    if (!dir.isEmpty()) {
        m_exportPath->setText(dir);
        QSettings s;
        s.setValue("export/path", dir);
    }
}

void ExportDialog::onExport() {
    QString dateUtc    = selectedDateUtc();
    QString exportPath = m_exportPath->text().trimmed();

    if (exportPath.isEmpty()) {
        QMessageBox::warning(this, "Export", "Please select an export folder.");
        return;
    }

    QSettings s;
    s.setValue("export/path", exportPath);

    auto contacts = m_log->contactsForDate(dateUtc);
    if (contacts.isEmpty()) {
        QMessageBox::information(this, "Export",
            "No contacts found for the selected date.");
        return;
    }

    QStringList files = AdifExporter::exportDate(contacts, exportPath, dateUtc);

    if (files.isEmpty()) {
        QMessageBox::warning(this, "Export",
            "Export failed — check application log for details.");
        return;
    }

    QStringList fileNames;
    for (const QString& f : files)
        fileNames << "  " + QFileInfo(f).fileName();

    QMessageBox::information(this, "Export Complete",
        QString("Export complete.\n\n%1\n\nFiles saved to:\n%2")
        .arg(fileNames.join("\n"), exportPath));

    accept();
}
