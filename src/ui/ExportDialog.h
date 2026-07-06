#pragma once
#include <QDialog>
#include <QRadioButton>
#include <QDateEdit>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QString>

class LogManager;

// ExportDialog — minimal two-option export UI.
//
// Operator selects: current UTC day (default) or specific past date.
// Preview shows file names to be generated before Export is clicked.
// Export folder defaults to Documents/HAVEN-FSK/, persisted in QSettings.

class ExportDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ExportDialog(LogManager* log, QWidget* parent = nullptr);

private slots:
    void onExport();
    void onBrowse();
    void onDateOptionChanged();
    void refreshPreview();

private:
    QString selectedDateUtc() const;
    QString defaultExportPath() const;

    LogManager*   m_log        {nullptr};

    QRadioButton* m_currentDay {nullptr};
    QRadioButton* m_selectDay  {nullptr};
    QDateEdit*    m_datePicker {nullptr};
    QLineEdit*    m_exportPath {nullptr};
    QPushButton*  m_browseBtn  {nullptr};
    QLabel*       m_preview    {nullptr};
    QPushButton*  m_exportBtn  {nullptr};
};
