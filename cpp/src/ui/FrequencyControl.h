#pragma once
#include <QWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QAction>
#include <QDoubleValidator>
#include <QFont>
#include <cstdint>
#include <algorithm>

// FrequencyControl — editable frequency display with increment buttons.
//
// Shows current radio frequency in MHz. Three tuning methods:
//   1. Click field, type frequency in MHz, press Enter
//   2. Click ▲/▼ to step by configured amount (default 100 Hz)
//   3. Right-click ▲/▼ to set step size (1/10/100/1000 Hz)
//
// Emits frequencyRequested(hz) when operator requests a change.
// Call setFrequency(hz) to update display from rig control feedback.
// Does NOT emit frequencyRequested when setFrequency() is called.

class FrequencyControl : public QWidget
{
    Q_OBJECT
public:
    explicit FrequencyControl(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(2);

        m_freqEdit = new QLineEdit(this);
        m_freqEdit->setFont(QFont("Courier New", 11, QFont::Bold));
        m_freqEdit->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_freqEdit->setMinimumWidth(115);
        m_freqEdit->setMaximumWidth(135);
        m_freqEdit->setStyleSheet(
            "QLineEdit {"
            "  background: #0d1117;"
            "  color: #ffaa00;"
            "  border: 1px solid #2a2a3e;"
            "  padding: 2px 4px;"
            "}"
            "QLineEdit:focus {"
            "  border: 1px solid #ffaa00;"
            "}");
        m_freqEdit->setValidator(
            new QDoubleValidator(0.0, 450.0, 6, this));
        m_freqEdit->setText("--");
        layout->addWidget(m_freqEdit);

        // ▲/▼ step buttons
        auto* btnCol = new QVBoxLayout;
        btnCol->setSpacing(1);
        m_upBtn   = new QPushButton("▲", this);
        m_downBtn = new QPushButton("▼", this);
        QString btnStyle =
            "QPushButton {"
            "  background: #1a1a2e;"
            "  color: #ffaa00;"
            "  border: 1px solid #2a2a3e;"
            "  font-size: 8pt;"
            "  padding: 0px;"
            "}"
            "QPushButton:hover { background: #0f3460; }"
            "QPushButton:pressed { background: #162447; }";
        for (auto* btn : {m_upBtn, m_downBtn}) {
            btn->setFixedSize(16, 11);
            btn->setStyleSheet(btnStyle);
            btn->setToolTip("Right-click to set step size");
            btn->setContextMenuPolicy(Qt::CustomContextMenu);
        }
        btnCol->addWidget(m_upBtn);
        btnCol->addWidget(m_downBtn);
        layout->addLayout(btnCol);

        auto* mhzLabel = new QLabel("MHz", this);
        mhzLabel->setStyleSheet("color: #888; font-size: 9pt;");
        layout->addWidget(mhzLabel);

        connect(m_freqEdit, &QLineEdit::returnPressed,
                this, &FrequencyControl::onEnterPressed);
        connect(m_upBtn,   &QPushButton::clicked,
                this, [this]() { onStep(+1); });
        connect(m_downBtn, &QPushButton::clicked,
                this, [this]() { onStep(-1); });

        for (auto* btn : {m_upBtn, m_downBtn}) {
            connect(btn, &QPushButton::customContextMenuRequested,
                    this, &FrequencyControl::onStepMenu);
        }
    }

    void setFrequency(uint64_t hz) {
        m_currentHz = hz;
        if (hz == 0)
            m_freqEdit->setText("--");
        else
            m_freqEdit->setText(
                QString::number(
                    static_cast<double>(hz) / 1.0e6, 'f', 6));
    }

    uint64_t frequency() const { return m_currentHz; }

signals:
    void frequencyRequested(uint64_t hz);

private slots:
    void onEnterPressed() {
        bool ok = false;
        double mhz = m_freqEdit->text().toDouble(&ok);
        if (!ok || mhz <= 0.0) return;
        m_currentHz = static_cast<uint64_t>(mhz * 1.0e6);
        emit frequencyRequested(m_currentHz);
    }

    void onStep(int dir) {
        if (m_currentHz == 0) return;
        int64_t n = static_cast<int64_t>(m_currentHz)
                  + dir * static_cast<int64_t>(m_stepHz);
        if (n > 0) {
            m_currentHz = static_cast<uint64_t>(n);
            setFrequency(m_currentHz);
            emit frequencyRequested(m_currentHz);
        }
    }

    void onStepMenu(const QPoint& pos) {
        QMenu menu(this);
        struct { int hz; const char* label; } steps[] = {
            {1,    "1 Hz"},
            {10,   "10 Hz"},
            {100,  "100 Hz"},
            {1000, "1 kHz"},
        };
        for (auto& st : steps) {
            QAction* a = menu.addAction(st.label);
            if (st.hz == m_stepHz) {
                a->setCheckable(true);
                a->setChecked(true);
            }
            int captHz = st.hz;
            connect(a, &QAction::triggered,
                    this, [this, captHz]() { m_stepHz = captHz; });
        }
        menu.exec(static_cast<QWidget*>(sender())->mapToGlobal(pos));
    }

private:
    QLineEdit*   m_freqEdit  {nullptr};
    QPushButton* m_upBtn     {nullptr};
    QPushButton* m_downBtn   {nullptr};
    uint64_t     m_currentHz {0};
    int          m_stepHz    {100};
};
