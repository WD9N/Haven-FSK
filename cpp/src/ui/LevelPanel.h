#pragma once
#include <QWidget>
#include <QFrame>
#include <QLabel>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPainter>
#include <QMouseEvent>
#include <QLinearGradient>
#include <cmath>
#include <algorithm>

// ── Shared constants ──────────────────────────────────────────────────────
static constexpr int   LED_COUNT    = 24;
static constexpr int   LED_SIZE     = 8;
static constexpr int   LED_GAP      = 3;
static constexpr float DB_MIN       = -34.0f;
static constexpr float DB_MAX       =   6.0f;
static constexpr int   CAP_H        = 28;
static constexpr int   STRIP_H      = LED_COUNT * LED_SIZE + (LED_COUNT - 1) * LED_GAP; // 261
static constexpr int   FADER_W      = 20;
static constexpr int   FADER_TRAVEL = STRIP_H - CAP_H; // 233

// ── LedStrip ─────────────────────────────────────────────────────────────
// 24 round LEDs, bottom = DB_MIN, top = DB_MAX, three color zones.
class LedStrip : public QWidget {
    Q_OBJECT
public:
    explicit LedStrip(QWidget* parent = nullptr) : QWidget(parent) {
        setFixedSize(LED_SIZE, STRIP_H);
    }
    void setLevel(float db) { m_db = db; update(); }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        for (int i = 0; i < LED_COUNT; i++) {
            float ledDb = DB_MIN + static_cast<float>(i) / (LED_COUNT - 1)
                          * (DB_MAX - DB_MIN);
            int y = STRIP_H - (i + 1) * LED_SIZE - i * LED_GAP;
            bool lit = ledDb <= m_db;
            QColor color;
            if      (ledDb < -6.0f) color = lit ? QColor(0x22,0xcc,0x44) : QColor(0x0a,0x2a,0x0f);
            else if (ledDb <  0.0f) color = lit ? QColor(0xff,0xcc,0x00) : QColor(0x2a,0x22,0x00);
            else                    color = lit ? QColor(0xff,0x22,0x22) : QColor(0x2a,0x04,0x04);
            p.setBrush(color);
            p.setPen(Qt::NoPen);
            p.drawEllipse(0, y, LED_SIZE, LED_SIZE);
        }
    }
private:
    float m_db = DB_MIN;
};

// ── FaderWidget ───────────────────────────────────────────────────────────
// Vertical fader. Track height = STRIP_H. Linear dB ↔ pixel mapping.
class FaderWidget : public QWidget {
    Q_OBJECT
public:
    explicit FaderWidget(QWidget* parent = nullptr)
        : QWidget(parent), m_db(-18.0f)
    {
        setFixedSize(FADER_W, STRIP_H);
        setCursor(Qt::PointingHandCursor);
        m_capY = dbToY(m_db);
    }

    float db() const { return m_db; }

    void setDb(float db) {
        m_db   = std::max(DB_MIN, std::min(DB_MAX, db));
        m_capY = dbToY(m_db);
        update();
    }

signals:
    void dbChanged(float db);

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        // Track
        p.setBrush(QColor(0x11,0x11,0x11));
        p.setPen(QColor(0x2a,0x2a,0x2a));
        p.drawRoundedRect(0, 0, FADER_W, STRIP_H, 3, 3);

        // Center rail
        p.setBrush(QColor(0x22,0x22,0x22));
        p.setPen(Qt::NoPen);
        p.drawRect(FADER_W/2 - 1, 0, 2, STRIP_H);

        // Cap
        int capX = (FADER_W - 18) / 2;
        QLinearGradient grad(capX, m_capY, capX, m_capY + CAP_H);
        grad.setColorAt(0.0, QColor(0x66,0x66,0x66));
        grad.setColorAt(0.4, QColor(0x44,0x44,0x44));
        grad.setColorAt(1.0, QColor(0x55,0x55,0x55));
        p.setBrush(grad);
        p.setPen(QColor(0x22,0x22,0x22));
        p.drawRoundedRect(capX, m_capY, 18, CAP_H, 2, 2);

        // Grip lines
        p.setPen(QColor(0x99,0x99,0x99));
        int mid = m_capY + CAP_H / 2;
        p.drawLine(capX + 3, mid - 2, capX + 14, mid - 2);
        p.drawLine(capX + 3, mid + 2, capX + 14, mid + 2);
    }

    void mousePressEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton) {
            m_dragging      = true;
            m_dragStartY    = e->pos().y();
            m_dragStartCapY = m_capY;
            setCursor(Qt::ClosedHandCursor);
        }
    }

    void mouseMoveEvent(QMouseEvent* e) override {
        if (!m_dragging) return;
        int newCapY = m_dragStartCapY + (e->pos().y() - m_dragStartY);
        newCapY = std::max(0, std::min(FADER_TRAVEL, newCapY));
        m_capY  = newCapY;
        m_db    = yToDb(m_capY);
        update();
        emit dbChanged(m_db);
    }

    void mouseReleaseEvent(QMouseEvent*) override {
        m_dragging = false;
        setCursor(Qt::PointingHandCursor);
    }

private:
    // capY=0 → DB_MAX, capY=FADER_TRAVEL → DB_MIN
    float yToDb(int y) const {
        float pct = 1.0f - static_cast<float>(y) / FADER_TRAVEL;
        return DB_MIN + pct * (DB_MAX - DB_MIN);
    }
    int dbToY(float db) const {
        float pct = (db - DB_MIN) / (DB_MAX - DB_MIN);
        return static_cast<int>((1.0f - pct) * FADER_TRAVEL);
    }

    bool  m_dragging      = false;
    int   m_dragStartY    = 0;
    int   m_dragStartCapY = 0;
    int   m_capY          = 0;
    float m_db            = -18.0f;
};

// ── ScaleWidget ───────────────────────────────────────────────────────────
class ScaleWidget : public QWidget {
public:
    explicit ScaleWidget(QWidget* parent = nullptr) : QWidget(parent) {
        setFixedSize(24, STRIP_H);
    }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setFont(QFont("Courier New", 7));

        struct Mark { float db; const char* label; bool bold; bool danger; };
        static const Mark marks[] = {
            {-34.f, "-34", false, false},
            {-28.f, "-28", false, false},
            {-22.f, "-22", false, false},
            {-16.f, "-16", false, false},
            {-10.f, "-10", false, false},
            { -6.f,  "-6", false, false},
            {  0.f,   "0", true,  false},
            {  6.f,  "+6", false, true },
        };

        for (const auto& m : marks) {
            float pct = (m.db - DB_MIN) / (DB_MAX - DB_MIN);
            int y = static_cast<int>((1.0f - pct) * STRIP_H);
            y = std::max(0, std::min(STRIP_H - 8, y));

            if (m.danger)     p.setPen(QColor(0xcc,0x44,0x44));
            else if (m.bold)  p.setPen(QColor(0xcc,0xaa,0x00));
            else              p.setPen(QColor(0x77,0x77,0x77));

            p.drawText(0, y, 22, 8,
                       Qt::AlignRight | Qt::AlignVCenter, m.label);
        }
    }
};

// ── ChannelStrip ──────────────────────────────────────────────────────────
class ChannelStrip : public QWidget {
    Q_OBJECT
public:
    explicit ChannelStrip(const QString& label,
                          const QString& subLabel,
                          QWidget* parent = nullptr)
        : QWidget(parent)
    {
        auto* vl = new QVBoxLayout(this);
        vl->setContentsMargins(0, 0, 0, 0);
        vl->setSpacing(3);

        auto* lbl = new QLabel(label, this);
        lbl->setAlignment(Qt::AlignCenter);
        lbl->setStyleSheet(
            "font-family:'Courier New'; font-size:10px;"
            "color:#aaa; letter-spacing:1px;");
        vl->addWidget(lbl);

        // dBu readout — static grey, never changes color
        m_dbLabel = new QLabel("-18 dBu", this);
        m_dbLabel->setAlignment(Qt::AlignCenter);
        m_dbLabel->setStyleSheet(
            "font-family:'Courier New'; font-size:10px; color:#999;");
        vl->addWidget(m_dbLabel);

        // Scale + LED + Fader row
        auto* row = new QHBoxLayout;
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(4);

        m_scale = new ScaleWidget(this);
        m_leds  = new LedStrip(this);
        m_fader = new FaderWidget(this);

        row->addWidget(m_scale);
        row->addWidget(m_leds);
        row->addWidget(m_fader);
        vl->addLayout(row);

        auto* sub = new QLabel(subLabel, this);
        sub->setAlignment(Qt::AlignCenter);
        sub->setStyleSheet(
            "font-family:'Courier New'; font-size:8px; color:#777;");
        vl->addWidget(sub);

        connect(m_fader, &FaderWidget::dbChanged,
                this, [this](float db) {
                    updateDbLabel(db);
                    emit faderChanged(db);
                });

        updateDbLabel(-18.0f);
    }

    void setMeterLevel(float db) { m_leds->setLevel(db); }

    void setFaderDb(float db) {
        m_fader->setDb(db);
        updateDbLabel(db);
    }

    float faderDb() const { return m_fader->db(); }

signals:
    void faderChanged(float db);

private:
    void updateDbLabel(float db) {
        QString sign = (db >= 0) ? "+" : "";
        m_dbLabel->setText(
            QString("%1%2 dBu").arg(sign).arg(static_cast<int>(db)));
    }

    QLabel*      m_dbLabel {nullptr};
    ScaleWidget* m_scale   {nullptr};
    LedStrip*    m_leds    {nullptr};
    FaderWidget* m_fader   {nullptr};
};

// ── LevelPanel ────────────────────────────────────────────────────────────
// Fixed-size panel with TX and RX channel strips side by side.
class LevelPanel : public QFrame {
    Q_OBJECT
public:
    explicit LevelPanel(QWidget* parent = nullptr) : QFrame(parent) {
        setFrameStyle(QFrame::NoFrame);
        setStyleSheet("background: #1a1a1a; border-right: 1px solid #2a2a2a;");
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

        auto* hl = new QHBoxLayout(this);
        hl->setContentsMargins(8, 8, 8, 8);
        hl->setSpacing(10);

        m_tx = new ChannelStrip("TX", "out", this);
        m_rx = new ChannelStrip("RX", "in",  this);

        hl->addWidget(m_tx);

        auto* div = new QFrame(this);
        div->setFrameShape(QFrame::VLine);
        div->setStyleSheet("color: #2a2a2a;");
        hl->addWidget(div);

        hl->addWidget(m_rx);

        m_tx->setFaderDb(-6.0f);
        m_tx->setToolTip(
            "TX audio output level\n"
            "0dBu = -6dBFS (nominal)\n"
            "Target: watch wattmeter, keep ALC inactive\n"
            "Full scale (+6dBu) = 0dBFS");
        m_rx->setFaderDb(-22.0f);

        connect(m_tx, &ChannelStrip::faderChanged,
                this, &LevelPanel::txFaderChanged);
        connect(m_rx, &ChannelStrip::faderChanged,
                this, &LevelPanel::rxFaderChanged);
    }

    // Update meters (dBFS input → dBu display, 0dBu = -6dBFS)
    void setRxLevel(float dbFS) { m_rx->setMeterLevel(dbFS + 6.0f); }
    void setTxLevel(float dbFS) { m_tx->setMeterLevel(dbFS + 6.0f); }

    // TX fader as dBFS (for QAudioOutput::setVolume)
    float txFaderDbFS() const { return m_tx->faderDb() - 6.0f; }

    // RX fader as linear gain multiplier
    float rxFaderGain() const {
        return std::pow(10.0f, (m_rx->faderDb() - 6.0f) / 20.0f);
    }

signals:
    void txFaderChanged(float dBu);
    void rxFaderChanged(float dBu);

private:
    ChannelStrip* m_tx {nullptr};
    ChannelStrip* m_rx {nullptr};
};
