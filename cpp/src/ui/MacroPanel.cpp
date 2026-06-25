#include "MacroPanel.h"
#include "../radio/RadioSettings.h"
#include <QGridLayout>
#include <QVBoxLayout>
#include <QFont>
#include <QSettings>
#include <QDialog>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QTextEdit>
#include <QLabel>
#include <QDebug>

MacroPanel::MacroPanel(QWidget* parent)
    : QWidget(parent)
{
    setupUi();
}

void MacroPanel::setupUi() {
    auto* grid = new QGridLayout(this);
    grid->setContentsMargins(4, 4, 4, 4);
    grid->setSpacing(4);

    for (int i = 0; i < NUM_MACROS; i++) {
        int row = i / 6;
        int col = i % 6;

        m_buttons[i] = new QPushButton(this);
        m_buttons[i]->setFixedWidth(100);
        m_buttons[i]->setFont(QFont("Arial", 8));
        m_buttons[i]->setContextMenuPolicy(Qt::CustomContextMenu);

        int captIdx = i;
        connect(m_buttons[i], &QPushButton::clicked,
                this, [this, captIdx]() { onMacroClicked(captIdx); });
        connect(m_buttons[i], &QPushButton::customContextMenuRequested,
                this, [this, captIdx](const QPoint&) { onMacroRightClicked(captIdx); });

        grid->addWidget(m_buttons[i], row, col, Qt::AlignLeft);
    }

    // Stretch column after the last button column keeps buttons left-aligned
    grid->setColumnStretch(6, 1);

    loadMacros();
}

void MacroPanel::loadMacros() {
    QSettings s;

    // Migration: old keys used "macros/bank0/N/label" and "macros/bank1/N/label"
    if (s.contains("macros/bank0/0/label") || s.contains("macros/bank0/0/text")) {
        qDebug() << "MacroPanel: migrating old bank0/bank1 format";
        for (int i = 0; i < 8; i++) {
            QString label = s.value(QString("macros/bank0/%1/label").arg(i)).toString();
            QString text  = s.value(QString("macros/bank0/%1/text").arg(i)).toString();
            if (!label.isEmpty()) {
                s.setValue(QString("macros/%1/label").arg(i), label);
                s.setValue(QString("macros/%1/text").arg(i),  text);
            }
            label = s.value(QString("macros/bank1/%1/label").arg(i)).toString();
            text  = s.value(QString("macros/bank1/%1/text").arg(i)).toString();
            if (!label.isEmpty()) {
                s.setValue(QString("macros/%1/label").arg(i + 8), label);
                s.setValue(QString("macros/%1/text").arg(i + 8),  text);
            }
        }
        s.remove("macros/bank0");
        s.remove("macros/bank1");
        qDebug() << "MacroPanel: migration complete";
    }

    struct Default { int idx; const char* label; const char* text; };
    static const Default defaults[] = {
        {0,  "CQ POTA",  "CQ POTA DE <myCall> <myParks> K"},
        {1,  "Stn Info", "DE <myCall> NAME:<myName> QTH:<myQTH> GRID:<myGrid> POTA:<myParks> RS:<rstSent> K"},
        {2,  "TU 73",    "<theirCall> DE <myCall> TU 73 SK"},
        {3,  "QRZ?",     "QRZ? DE <myCall> K"},
        {4,  "AGN?",     "<theirCall> DE <myCall> AGN? K"},
        {5,  "QSL",      "<theirCall> DE <myCall> QSL TU K"},
        {6,  "CQ",       "CQ CQ DE <myCall> <myCall> K"},
        {7,  "Stn Info", "DE <myCall> NAME:<myName> QTH:<myQTH> GRID:<myGrid> K"},
        {8,  "TU 73",    "<theirCall> DE <myCall> TU 73 K"},
        {9,  "AGN?",     "<theirCall> DE <myCall> AGN? K"},
    };

    for (int i = 0; i < NUM_MACROS; i++) {
        QString savedLabel = s.value(QString("macros/%1/label").arg(i)).toString();
        QString savedText  = s.value(QString("macros/%1/text").arg(i)).toString();

        if (!savedLabel.isEmpty()) {
            m_macroLabel[i] = savedLabel;
            m_macroText[i]  = savedText;
        } else {
            m_macroLabel[i].clear();
            m_macroText[i].clear();
            for (const auto& d : defaults) {
                if (d.idx == i) {
                    m_macroLabel[i] = d.label;
                    m_macroText[i]  = d.text;
                    break;
                }
            }
        }
        updateButton(i);
    }
}

void MacroPanel::saveMacros() {
    QSettings s;
    for (int i = 0; i < NUM_MACROS; i++) {
        s.setValue(QString("macros/%1/label").arg(i), m_macroLabel[i]);
        s.setValue(QString("macros/%1/text").arg(i),  m_macroText[i]);
    }
}

void MacroPanel::updateButton(int index) {
    const QString& label = m_macroLabel[index];
    bool isEmpty = label.isEmpty();

    m_buttons[index]->setText(
        isEmpty ? QString("(%1)").arg(index + 1) : label);
    m_buttons[index]->setEnabled(true);

    if (isEmpty) {
        m_buttons[index]->setStyleSheet(
            "QPushButton {"
            "  background: #0d0d1a; color: #444;"
            "  border: 1px dashed #333; font-size: 8pt;"
            "}"
            "QPushButton:hover { background: #1a1a2e; color: #666; }");
        m_buttons[index]->setToolTip("Right-click to configure this macro");
    } else {
        m_buttons[index]->setStyleSheet(
            "QPushButton {"
            "  background: #1a1a2e; color: #ccc;"
            "  border: 1px solid #333; font-size: 8pt;"
            "}"
            "QPushButton:hover { background: #0f3460; }"
            "QPushButton:pressed { background: #162447; }");
        m_buttons[index]->setToolTip(
            "Left-click: send macro\nRight-click: edit macro");
    }
}

void MacroPanel::onMacroClicked(int index) {
    QString raw = m_macroText[index];
    if (raw.trimmed().isEmpty()) return;

    QString text = expandMacro(raw);
    text.remove("<TX>", Qt::CaseInsensitive);
    text = text.trimmed();

    emit macroTriggered(text);
}

void MacroPanel::onMacroRightClicked(int index) {
    QDialog dlg(this);
    dlg.setWindowTitle(QString("Edit Macro %1").arg(index + 1));
    dlg.setMinimumWidth(420);

    auto* form      = new QFormLayout;
    auto* labelEdit = new QLineEdit(m_macroLabel[index]);
    auto* textEdit  = new QTextEdit;
    textEdit->setPlainText(m_macroText[index]);
    textEdit->setMinimumHeight(80);
    textEdit->setFont(QFont("Courier New", 9));

    auto* hint = new QLabel(
        "Tags: &lt;myCall&gt; &lt;myParks&gt; &lt;mySOTA&gt; "
        "&lt;myGrid&gt; &lt;myName&gt; &lt;myQTH&gt; "
        "&lt;myState&gt; &lt;myCounty&gt; "
        "&lt;theirCall&gt; &lt;rstSent&gt;");
    hint->setWordWrap(true);
    hint->setStyleSheet("color: gray; font-size: 8pt;");

    form->addRow("Button Label:", labelEdit);
    form->addRow("Macro Text:",   textEdit);
    form->addRow("",              hint);

    auto* btns = new QDialogButtonBox(
        QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    auto* layout = new QVBoxLayout(&dlg);
    layout->addLayout(form);
    layout->addWidget(btns);

    if (dlg.exec() == QDialog::Accepted) {
        m_macroLabel[index] = labelEdit->text().trimmed();
        m_macroText[index]  = textEdit->toPlainText().trimmed();
        saveMacros();
        updateButton(index);
    }
}

QString MacroPanel::expandMacro(const QString& text) const {
    HavenFSK::StationInfo info = HavenFSK::loadStationInfo();
    QString result = text;

    result.replace("<myCall>",    info.callsign,  Qt::CaseInsensitive);
    result.replace("<myParks>",   info.myParks(),  Qt::CaseInsensitive);
    result.replace("<mySOTA>",    info.sotaRef,    Qt::CaseInsensitive);
    result.replace("<myGrid>",    info.grid,       Qt::CaseInsensitive);
    result.replace("<myName>",    info.opName,     Qt::CaseInsensitive);
    result.replace("<myQTH>",     info.opName,     Qt::CaseInsensitive);
    result.replace("<myFD>",
        info.fdClass + (info.fdSection.isEmpty() ? "" : " " + info.fdSection),
        Qt::CaseInsensitive);
    result.replace("<myState>",   info.state,      Qt::CaseInsensitive);
    result.replace("<myCounty>",  info.county,     Qt::CaseInsensitive);
    result.replace("<theirCall>", m_theirCall,     Qt::CaseInsensitive);
    result.replace("<rstSent>",   m_rsSent,        Qt::CaseInsensitive);

    return result;
}
