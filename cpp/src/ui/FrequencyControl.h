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
#include <QFontMetrics>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QPalette>
#include <QTimer>
#include <cstdint>
#include <algorithm>

// FrequencyControl — editable frequency display with increment buttons.
//
// Always editable — works with or without rig control (Fix 11).
// Amber text: rig-controlled frequency.
// Grey text: manual entry mode (no rig connected).
//
// Emits frequencyRequested(hz) when operator requests a change.
// Call setFrequency(hz) to update display from rig control feedback.

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
        m_freqEdit->setValidator(
            new QDoubleValidator(0.0, 450.0, 6, this));
        m_freqEdit->setPlaceholderText("Enter MHz");
        applyStyle(false);  // start in manual mode

        // Digit scroll tuning — event filter handles wheel and hover
        m_freqEdit->installEventFilter(this);
        m_freqEdit->setMouseTracking(true);

        // Highlight palette: dark blue bg, amber text — matches frequency color
        QPalette pal = m_freqEdit->palette();
        pal.setColor(QPalette::Highlight,        QColor(0x2a, 0x2a, 0x5a));
        pal.setColor(QPalette::HighlightedText,  QColor(0xff, 0xaa, 0x00));
        m_freqEdit->setPalette(pal);

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

    // Update display from rig control (amber style)
    void setFrequency(uint64_t hz) {
        m_currentHz      = hz;
        m_rigControlled  = (hz > 0);
        if (hz == 0) {
            m_freqEdit->setText("");
            m_freqEdit->setPlaceholderText("Enter MHz");
            applyStyle(false);
        } else {
            m_freqEdit->setText(
                QString::number(static_cast<double>(hz) / 1.0e6, 'f', 6));
            applyStyle(true);
        }
        m_freqEdit->setToolTip(
            m_rigControlled
            ? "Frequency from rig control — click ▲▼ to adjust"
            : "No rig control — type frequency in MHz and press Enter");
    }

    uint64_t frequency() const { return m_currentHz; }

signals:
    void frequencyRequested(uint64_t hz);

protected:
    bool eventFilter(QObject* obj, QEvent* event) override {
        if (obj != m_freqEdit)
            return QWidget::eventFilter(obj, event);

        if (event->type() == QEvent::Wheel) {
            auto* we = static_cast<QWheelEvent*>(event);
            onDigitWheel(we);
            return true;
        }
        if (event->type() == QEvent::MouseMove) {
            auto* me = static_cast<QMouseEvent*>(event);
            onDigitHover(me->pos().x());
            return false;
        }
        if (event->type() == QEvent::Leave) {
            m_freqEdit->deselect();
            m_hoveredDigit = -1;
            return false;
        }
        return QWidget::eventFilter(obj, event);
    }

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
    static constexpr uint64_t FREQ_MIN_HZ =  1000000ULL;  // 1 MHz
    static constexpr uint64_t FREQ_MAX_HZ = 30000000ULL;  // 30 MHz

    // Map x pixel to character index in the frequency string.
    // Text is right-aligned, so compute text start from contentsRect.
    // Format: "14.074000" — 9 chars, indices 0-8.
    int digitAtX(int x) const {
        QString text = m_freqEdit->text();
        if (text.isEmpty()) return -1;

        QFontMetrics fm(m_freqEdit->font());
        int charWidth = fm.horizontalAdvance(QChar('0'));

        // Right-aligned: text ends at contentsRect right - 4px stylesheet padding
        int rightEdge  = m_freqEdit->contentsRect().right() - 4;
        int textStartX = rightEdge - charWidth * text.length();

        int charIndex = (x - textStartX) / charWidth;
        if (charIndex < 0 || charIndex >= text.length()) return -1;

        QChar c = text.at(charIndex);
        if (c == '.') return -1;

        return charIndex;
    }

    // Hz step for character index in "14.074000":
    // idx 0='1'(10M) 1='4'(1M) 2='.'  3='0'(100k) 4='7'(10k)
    // idx 5='4'(1k)  6='.'     7='0'(100Hz) 8='0'(10Hz) 9='0'(1Hz)
    uint64_t stepForDigitIndex(int idx) const {
        switch (idx) {
            case 0: return 10000000ULL;  // 10 MHz — disabled in hover
            case 1: return  1000000ULL;  // 1 MHz
            case 3: return   100000ULL;  // 100 kHz
            case 4: return    10000ULL;  // 10 kHz
            case 5: return     1000ULL;  // 1 kHz
            case 7: return      100ULL;  // 100 Hz
            case 8: return       10ULL;  // 10 Hz
            case 9: return        1ULL;  // 1 Hz
            default: return       0ULL;
        }
    }

    void onDigitHover(int x) {
        int digit = digitAtX(x);
        if (digit == m_hoveredDigit) return;
        m_hoveredDigit = digit;

        if (digit > 0) {  // index 0 (10MHz) disabled
            m_freqEdit->setSelection(digit, 1);
        } else {
            m_freqEdit->deselect();
        }
    }

    void onDigitWheel(QWheelEvent* e) {
        if (m_currentHz == 0) return;
        if (m_hoveredDigit <= 0) return;  // -1 = none, 0 = 10MHz disabled

        uint64_t step = stepForDigitIndex(m_hoveredDigit);
        if (step == 0) return;

        int delta = e->angleDelta().y() > 0 ? 1 : -1;

        int64_t newHz = static_cast<int64_t>(m_currentHz)
                      + delta * static_cast<int64_t>(step);

        newHz = std::max(static_cast<int64_t>(FREQ_MIN_HZ),
                std::min(static_cast<int64_t>(FREQ_MAX_HZ), newHz));

        // Zero all digits below the scrolled digit
        uint64_t rounded = (static_cast<uint64_t>(newHz) / step) * step;

        setFrequency(rounded);
        emit frequencyRequested(rounded);

        // Restore selection after setText clears it
        QTimer::singleShot(0, this, [this]() {
            if (m_hoveredDigit > 0)
                m_freqEdit->setSelection(m_hoveredDigit, 1);
        });
    }

    void applyStyle(bool rigControlled) {
        QString color = rigControlled ? "#ffaa00" : "#888888";
        m_freqEdit->setStyleSheet(
            QString("QLineEdit {"
                    "  background: #0d1117;"
                    "  color: %1;"
                    "  border: 1px solid #2a2a3e;"
                    "  padding: 2px 4px;"
                    "}"
                    "QLineEdit:focus {"
                    "  border: 1px solid %1;"
                    "}").arg(color));
    }

    QLineEdit*   m_freqEdit     {nullptr};
    QPushButton* m_upBtn        {nullptr};
    QPushButton* m_downBtn      {nullptr};
    uint64_t     m_currentHz    {0};
    int          m_stepHz       {100};
    bool         m_rigControlled{false};
    int          m_hoveredDigit {-1};
};
