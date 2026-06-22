#include "MacroPanel.h"
#include "../radio/RadioSettings.h"
#include <QSettings>
#include <QDialog>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QTextEdit>
#include <QLabel>
#include <QVBoxLayout>
#include <QStackedWidget>
#include <QFont>

MacroPanel::MacroPanel(QWidget* parent)
    : QWidget(parent)
{
    setupUi();
    loadMacros();
}

void MacroPanel::setupUi() {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    // Bank selector row
    auto* bankRow = new QHBoxLayout;
    m_bankA = new QPushButton("A");
    m_bankB = new QPushButton("B");
    m_bankA->setMaximumWidth(30);
    m_bankB->setMaximumWidth(30);
    m_bankA->setCheckable(true);
    m_bankB->setCheckable(true);
    m_bankA->setChecked(true);
    bankRow->addWidget(new QLabel("Macros:"));
    bankRow->addWidget(m_bankA);
    bankRow->addWidget(m_bankB);
    bankRow->addStretch();
    layout->addLayout(bankRow);

    auto* stack = new QStackedWidget(this);

    for (int bank = 0; bank < NUM_BANKS; bank++) {
        m_bankWidget[bank] = new QWidget;
        auto* row = new QHBoxLayout(m_bankWidget[bank]);
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(3);

        for (int i = 0; i < NUM_MACROS; i++) {
            m_buttons[bank][i] = new QPushButton;
            m_buttons[bank][i]->setMinimumWidth(70);
            m_buttons[bank][i]->setMaximumWidth(110);
            m_buttons[bank][i]->setFont(QFont("Arial", 8));
            m_buttons[bank][i]->setToolTip(
                "Left-click: send macro\nRight-click: edit macro");
            m_buttons[bank][i]->setContextMenuPolicy(Qt::CustomContextMenu);

            int captBank = bank, captIdx = i;
            connect(m_buttons[bank][i], &QPushButton::clicked,
                    this, [this, captBank, captIdx]() {
                        onMacroClicked(captBank, captIdx);
                    });
            connect(m_buttons[bank][i],
                    &QPushButton::customContextMenuRequested,
                    this, [this, captBank, captIdx](const QPoint&) {
                        onMacroRightClicked(captBank, captIdx);
                    });
            row->addWidget(m_buttons[bank][i]);
        }
        stack->addWidget(m_bankWidget[bank]);
    }
    layout->addWidget(stack);

    connect(m_bankA, &QPushButton::clicked, this, [this, stack]() {
        m_bankB->setChecked(false);
        m_bankA->setChecked(true);
        stack->setCurrentIndex(0);
        m_currentBank = 0;
    });
    connect(m_bankB, &QPushButton::clicked, this, [this, stack]() {
        m_bankA->setChecked(false);
        m_bankB->setChecked(true);
        stack->setCurrentIndex(1);
        m_currentBank = 1;
    });
}

void MacroPanel::loadMacros() {
    QSettings s;
    for (int bank = 0; bank < NUM_BANKS; bank++) {
        for (int i = 0; i < NUM_MACROS; i++) {
            QString labelKey = QString("macros/bank%1/%2/label").arg(bank).arg(i);
            QString textKey  = QString("macros/bank%1/%2/text").arg(bank).arg(i);
            m_macroLabel[bank][i] = s.value(labelKey, defaultLabel(bank, i)).toString();
            m_macroText[bank][i]  = s.value(textKey,  defaultMacro(bank, i)).toString();
            updateButton(bank, i);
        }
    }
}

void MacroPanel::saveMacro(int bank, int index) {
    QSettings s;
    s.setValue(QString("macros/bank%1/%2/label").arg(bank).arg(index),
               m_macroLabel[bank][index]);
    s.setValue(QString("macros/bank%1/%2/text").arg(bank).arg(index),
               m_macroText[bank][index]);
}

void MacroPanel::updateButton(int bank, int index) {
    // Fix 6: always enable so right-click works on empty slots
    const QString& label = m_macroLabel[bank][index];
    bool isEmpty = label.isEmpty();

    m_buttons[bank][index]->setText(
        isEmpty ? QString("(%1)").arg(index + 1) : label);
    m_buttons[bank][index]->setEnabled(true);  // always enabled

    if (isEmpty) {
        m_buttons[bank][index]->setStyleSheet(
            "QPushButton {"
            "  background: #0d0d1a;"
            "  color: #444;"
            "  border: 1px dashed #333;"
            "  font-size: 8pt;"
            "}"
            "QPushButton:hover { background: #1a1a2e; color: #666; }");
        m_buttons[bank][index]->setToolTip(
            "Right-click to configure this macro");
    } else {
        m_buttons[bank][index]->setStyleSheet(
            "QPushButton {"
            "  background: #1a1a2e;"
            "  color: #ccc;"
            "  border: 1px solid #333;"
            "  font-size: 8pt;"
            "}"
            "QPushButton:hover { background: #0f3460; }"
            "QPushButton:pressed { background: #162447; }");
        m_buttons[bank][index]->setToolTip(
            "Left-click: send macro\n"
            "Right-click: edit macro");
    }
}

void MacroPanel::onMacroClicked(int bank, int index) {
    QString raw = m_macroText[bank][index];
    if (raw.trimmed().isEmpty()) return;  // Fix 6: unconfigured, do nothing

    bool autoTx = raw.contains("<TX>", Qt::CaseInsensitive);
    QString text = expandMacro(raw);
    text.remove("<TX>", Qt::CaseInsensitive);
    text = text.trimmed();

    emit macroTriggered(text, autoTx);
}

void MacroPanel::onMacroRightClicked(int bank, int index) {
    QDialog dlg(this);
    dlg.setWindowTitle(QString("Edit Macro %1-%2")
                       .arg(bank == 0 ? "A" : "B").arg(index + 1));
    dlg.setMinimumWidth(400);

    auto* form      = new QFormLayout;
    auto* labelEdit = new QLineEdit(m_macroLabel[bank][index]);
    auto* textEdit  = new QTextEdit;
    textEdit->setPlainText(m_macroText[bank][index]);
    textEdit->setMinimumHeight(80);
    textEdit->setFont(QFont("Courier New", 9));

    auto* hint = new QLabel(
        "Tags: &lt;myCall&gt; &lt;myParks&gt; &lt;mySOTA&gt; "
        "&lt;myGrid&gt; &lt;myName&gt; &lt;myQTH&gt; "
        "&lt;theirCall&gt; &lt;rstSent&gt; &lt;TX&gt;<br>"
        "&lt;TX&gt; at end triggers automatic transmission.");
    hint->setWordWrap(true);
    hint->setStyleSheet("color: gray; font-size: 8pt;");

    form->addRow("Button Label:", labelEdit);
    form->addRow("Macro Text:",   textEdit);
    form->addRow("",              hint);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    auto* layout = new QVBoxLayout(&dlg);
    layout->addLayout(form);
    layout->addWidget(buttons);

    if (dlg.exec() == QDialog::Accepted) {
        m_macroLabel[bank][index] = labelEdit->text().trimmed();
        m_macroText[bank][index]  = textEdit->toPlainText().trimmed();
        saveMacro(bank, index);
        updateButton(bank, index);
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
    result.replace("<theirCall>", m_theirCall,     Qt::CaseInsensitive);
    result.replace("<rstSent>",   m_rsSent,        Qt::CaseInsensitive);

    return result;
}

QString MacroPanel::defaultLabel(int bank, int index) const {
    static const QString bankALabels[8] = {
        "CQ POTA", "Stn Info", "TU 73", "QRZ?", "AGN?", "QSL", "", ""
    };
    static const QString bankBLabels[8] = {
        "CQ", "Stn Info", "TU 73", "AGN?", "", "", "", ""
    };
    return bank == 0 ? bankALabels[index] : bankBLabels[index];
}

QString MacroPanel::defaultMacro(int bank, int index) const {
    static const QString bankA[8] = {
        "CQ POTA DE <myCall> <myParks> K <TX>",
        "<theirCall> DE <myCall> NAME:<myName> QTH:<myQTH> "
        "GRID:<myGrid> POTA:<myParks> RS:<rstSent> K <TX>",
        "<theirCall> DE <myCall> TU 73 K <TX>",
        "QRZ? DE <myCall> <myParks> K <TX>",
        "<theirCall> DE <myCall> AGN? K <TX>",
        "<theirCall> DE <myCall> QSL TNX K <TX>",
        "", ""
    };
    static const QString bankB[8] = {
        "CQ CQ DE <myCall> <myCall> K <TX>",
        "<theirCall> DE <myCall> NAME:<myName> QTH:<myQTH> "
        "GRID:<myGrid> RS:<rstSent> K <TX>",
        "<theirCall> DE <myCall> TU 73 K <TX>",
        "<theirCall> DE <myCall> AGN? K <TX>",
        "", "", "", ""
    };
    return bank == 0 ? bankA[index] : bankB[index];
}
