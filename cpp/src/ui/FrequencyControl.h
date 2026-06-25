#pragma once
#include <QWidget>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QMenu>
#include <QAction>
#include <QFont>
#include <QFontMetrics>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QPainter>
#include <cstdint>
#include <algorithm>

// ── DigitDisplay ──────────────────────────────────────────────────────────────
// Custom frequency display with digit-scroll tuning.
// Paints its own text and highlight box — no QLineEdit text cursor.
// System cursor hidden (BlankCursor) when over a scrollable digit.
//
// Format: "14.074000" (MHz, 6 dp)
// Index:   0=10MHz(disabled) 1=1MHz 2=. 3=100kHz 4=10kHz
//          5=1kHz 6=. 7=100Hz 8=10Hz 9=1Hz
class DigitDisplay : public QWidget {
    Q_OBJECT
public:
    explicit DigitDisplay(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setFont(QFont("Courier New", 11, QFont::Bold));
        setCursor(Qt::ArrowCursor);
        setMouseTracking(true);
        setMinimumWidth(115);
        setMaximumWidth(155);
        setFixedHeight(fontMetrics().height() + 8);
        setFocusPolicy(Qt::ClickFocus);
    }

    void setFrequencyHz(uint64_t hz) {
        m_hz = hz;
        if (hz == 0) {
            m_text.clear();
            m_placeholder = true;
        } else {
            m_text = QString::number(
                static_cast<double>(hz) / 1.0e6, 'f', 6);
            m_placeholder = false;
        }
        update();
    }

    void setRigControlled(bool rig) {
        m_rigControlled = rig;
        update();
    }

    uint64_t hz() const { return m_hz; }

signals:
    void frequencyRequested(uint64_t hz);

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, false);
        p.fillRect(rect(), QColor(0x0d, 0x11, 0x17));

        QFontMetrics fm(font());
        int charW = fm.horizontalAdvance('0');
        int charH = fm.height();
        int textY = (height() + charH) / 2 - fm.descent();
        int textX = 4;

        if (m_placeholder || m_text.isEmpty()) {
            p.setPen(QColor(0x55, 0x55, 0x55));
            p.drawText(textX, textY, "Enter MHz");
            return;
        }

        // Highlight box behind hovered digit
        if (m_hoveredDigit >= 0 &&
            m_hoveredDigit < m_text.length() &&
            m_text.at(m_hoveredDigit) != '.') {
            int boxX = textX + m_hoveredDigit * charW;
            QRect box(boxX - 1, 2, charW + 2, height() - 4);
            p.fillRect(box, QColor(0x2a, 0x2a, 0x5a));
            p.setPen(QColor(0x44, 0x44, 0x88));
            p.drawRect(box.adjusted(0, 0, -1, -1));
        }

        QColor normalColor = m_rigControlled
            ? QColor(0xff, 0xaa, 0x00)
            : QColor(0x88, 0x88, 0x88);

        for (int i = 0; i < m_text.length(); i++) {
            int  cx = textX + i * charW;
            QChar c = m_text.at(i);

            if (c == '.') {
                p.setPen(normalColor.darker(150));
            } else if (i == m_hoveredDigit) {
                p.setPen(QColor(0xff, 0xcc, 0x44));  // bright amber on hover
            } else {
                p.setPen(normalColor);
            }
            p.drawText(cx, textY, QString(c));
        }

        // "MHz" suffix
        p.setPen(QColor(0x55, 0x55, 0x55));
        p.setFont(QFont("Arial", 8));
        p.drawText(textX + m_text.length() * charW + 4, textY, "MHz");
    }

    void mouseMoveEvent(QMouseEvent* e) override {
        if (m_text.isEmpty()) return;

        QFontMetrics fm(font());
        int charW = fm.horizontalAdvance('0');
        int textX = 4;

        int idx = (e->pos().x() - textX) / charW;
        idx = std::max(0, std::min(static_cast<int>(m_text.length()) - 1, idx));

        // Dots and 10MHz digit (index 0) are not interactive
        if (m_text.at(idx) == '.' || idx == 0) {
            if (m_hoveredDigit != -1) {
                m_hoveredDigit = -1;
                setCursor(Qt::ArrowCursor);
                update();
            }
            return;
        }

        if (idx != m_hoveredDigit) {
            m_hoveredDigit = idx;
            setCursor(Qt::BlankCursor);  // highlight box is sufficient feedback
            update();
        }
    }

    void leaveEvent(QEvent*) override {
        m_hoveredDigit = -1;
        setCursor(Qt::ArrowCursor);
        update();
    }

    void wheelEvent(QWheelEvent* e) override {
        if (m_hz == 0 || m_hoveredDigit < 0) return;
        if (m_text.at(m_hoveredDigit) == '.' || m_hoveredDigit == 0) return;

        uint64_t step = stepForIndex(m_hoveredDigit);
        if (step == 0) return;

        int delta = e->angleDelta().y() > 0 ? 1 : -1;

        int64_t newHz = static_cast<int64_t>(m_hz)
                      + delta * static_cast<int64_t>(step);

        newHz = std::max(static_cast<int64_t>(1000000),
                std::min(static_cast<int64_t>(30000000), newHz));

        // Zero all digits below the scrolled digit
        uint64_t rounded = (static_cast<uint64_t>(newHz) / step) * step;

        setFrequencyHz(rounded);
        emit frequencyRequested(rounded);
        update();  // repaint with m_hoveredDigit unchanged
    }

    void mousePressEvent(QMouseEvent*) override {
        setFocus();
    }

    void keyPressEvent(QKeyEvent* e) override {
        if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
            bool ok   = false;
            double mhz = m_text.toDouble(&ok);
            if (ok && mhz >= 1.0 && mhz <= 30.0) {
                uint64_t hz = static_cast<uint64_t>(mhz * 1.0e6);
                setFrequencyHz(hz);
                emit frequencyRequested(hz);
            }
            return;
        }
        QWidget::keyPressEvent(e);
    }

private:
    uint64_t stepForIndex(int idx) const {
        switch (idx) {
            case 1: return 1000000ULL;
            case 3: return  100000ULL;
            case 4: return   10000ULL;
            case 5: return    1000ULL;
            case 7: return     100ULL;
            case 8: return      10ULL;
            case 9: return       1ULL;
            default: return      0ULL;
        }
    }

    uint64_t m_hz           {0};
    QString  m_text;
    bool     m_placeholder  {true};
    bool     m_rigControlled{false};
    int      m_hoveredDigit {-1};
};

// ── FrequencyControl ──────────────────────────────────────────────────────────
// Frequency display + tuning control using DigitDisplay.
// Amber: rig-controlled frequency. Grey: manual / no rig.
// Emits frequencyRequested(hz) on digit scroll or ▲/▼ button.
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

        m_freqDisplay = new DigitDisplay(this);
        layout->addWidget(m_freqDisplay);

        // ▲/▼ step buttons — complement digit scroll for non-mouse input
        auto* btnCol  = new QVBoxLayout;
        btnCol->setSpacing(1);
        m_upBtn   = new QPushButton("▲", this);
        m_downBtn = new QPushButton("▼", this);
        QString btnStyle =
            "QPushButton {"
            "  background: #1a1a2e; color: #ffaa00;"
            "  border: 1px solid #2a2a3e; font-size: 8pt; padding: 0px;"
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

        connect(m_freqDisplay, &DigitDisplay::frequencyRequested,
                this,           &FrequencyControl::frequencyRequested);
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
        m_currentHz     = hz;
        m_rigControlled = (hz > 0);
        m_freqDisplay->setFrequencyHz(hz);
        m_freqDisplay->setRigControlled(m_rigControlled);
        m_freqDisplay->setToolTip(
            m_rigControlled
            ? "Hover a digit and scroll to tune\n10MHz digit disabled"
            : "No rig control — digit scroll inactive");
    }

    uint64_t frequency() const { return m_currentHz; }

signals:
    void frequencyRequested(uint64_t hz);

private slots:
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
    DigitDisplay* m_freqDisplay   {nullptr};
    QPushButton*  m_upBtn         {nullptr};
    QPushButton*  m_downBtn       {nullptr};
    uint64_t      m_currentHz     {0};
    int           m_stepHz        {100};
    bool          m_rigControlled {false};
};
