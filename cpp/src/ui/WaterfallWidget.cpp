#include "WaterfallWidget.h"
#include <QPainter>
#include <QPen>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QSettings>
#include <QSizePolicy>
#include <cmath>
#include <algorithm>
#include <cstring>
#include "kiss_fft.h"

namespace {

void buildEarthPalette(QRgb* p) {
    for (int i = 0; i < 256; i++) {
        float t = i / 255.0f;
        int r, g, b;
        if (t < 0.25f) {
            float u = t / 0.25f;
            r = static_cast<int>(u * 45);
            g = static_cast<int>(u * 22); b = 0;
        } else if (t < 0.5f) {
            float u = (t - 0.25f) / 0.25f;
            r = static_cast<int>(45 + u * 62);
            g = static_cast<int>(22 + u * 36); b = 0;
        } else if (t < 0.75f) {
            float u = (t - 0.5f) / 0.25f;
            r = static_cast<int>(107 + u * 93);
            g = static_cast<int>(58 + u * 75);
            b = static_cast<int>(u * 10);
        } else {
            float u = (t - 0.75f) / 0.25f;
            r = static_cast<int>(200 + u * 55);
            g = static_cast<int>(133 + u * 100);
            b = static_cast<int>(10 + u * 200);
        }
        p[i] = qRgb(std::clamp(r, 0, 255),
                    std::clamp(g, 0, 255),
                    std::clamp(b, 0, 255));
    }
}

void buildClassicPalette(QRgb* p) {
    for (int i = 0; i < 256; i++) {
        float t = i / 255.0f;
        int r, g, b;
        if (t < 0.25f) {
            float u = t / 0.25f;
            r = 0; g = 0; b = static_cast<int>(u * 180);
        } else if (t < 0.5f) {
            float u = (t - 0.25f) / 0.25f;
            r = 0;
            g = static_cast<int>(u * 200);
            b = static_cast<int>(180 + u * 75);
        } else if (t < 0.75f) {
            float u = (t - 0.5f) / 0.25f;
            r = static_cast<int>(u * 255);
            g = static_cast<int>(200 + u * 55);
            b = 0;
        } else {
            float u = (t - 0.75f) / 0.25f;
            r = 255; g = 255;
            b = static_cast<int>(u * 255);
        }
        p[i] = qRgb(std::clamp(r, 0, 255),
                    std::clamp(g, 0, 255),
                    std::clamp(b, 0, 255));
    }
}

void buildGreyscalePalette(QRgb* p) {
    for (int i = 0; i < 256; i++)
        p[i] = qRgb(i, i, i);
}

void buildNightPalette(QRgb* p) {
    for (int i = 0; i < 256; i++) {
        float t = i / 255.0f;
        int r, g = 0, b = 0;
        if (t < 0.4f) {
            float u = t / 0.4f;
            r = static_cast<int>(u * 180);
        } else if (t < 0.7f) {
            float u = (t - 0.4f) / 0.3f;
            r = static_cast<int>(180 + u * 75);
            g = static_cast<int>(u * 140);
        } else {
            float u = (t - 0.7f) / 0.3f;
            r = 255;
            g = static_cast<int>(140 + u * 115);
            b = static_cast<int>(u * 100);
        }
        p[i] = qRgb(std::clamp(r, 0, 255),
                    std::clamp(g, 0, 255),
                    std::clamp(b, 0, 255));
    }
}

} // anonymous namespace

WaterfallWidget::WaterfallWidget(QWidget* parent)
    : QWidget(parent)
{
    setFocusPolicy(Qt::ClickFocus);
    setMinimumHeight(80);
    m_overlapBuffer.reserve(FFT_SIZE * 2);
    m_fftCfg = kiss_fft_alloc(FFT_SIZE, 0, nullptr, nullptr);
    buildEarthPalette(m_palette);

    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);

    // ── Toolbar ───────────────────────────────────────────────────────────
    m_toolbar = new QWidget(this);
    m_toolbar->setFixedHeight(28);
    m_toolbar->setStyleSheet("background: #111118;");
    auto* tl = new QHBoxLayout(m_toolbar);
    tl->setContentsMargins(4, 2, 4, 2);
    tl->setSpacing(8);

    auto* rl = new QLabel("Range:", m_toolbar);
    rl->setStyleSheet("color: #aaa; font-size: 9pt;");
    tl->addWidget(rl);
    m_rangeCombo = new QComboBox(m_toolbar);
    m_rangeCombo->addItems({"1.5 kHz","2.0 kHz","2.5 kHz",
                             "3.0 kHz","3.5 kHz","4.0 kHz"});
    m_rangeCombo->setCurrentIndex(3);
    m_rangeCombo->setMaximumWidth(78);
    m_rangeCombo->setStyleSheet(
        "background:#1a1a2e; color:#ccc; border:1px solid #333;");
    tl->addWidget(m_rangeCombo);

    auto* sl = new QLabel("Speed:", m_toolbar);
    sl->setStyleSheet("color: #aaa; font-size: 9pt;");
    tl->addWidget(sl);
    m_speedSlider = new QSlider(Qt::Horizontal, m_toolbar);
    m_speedSlider->setRange(0, 3);
    m_speedSlider->setValue(0);
    m_speedSlider->setMaximumWidth(72);
    m_speedSlider->setTickPosition(QSlider::TicksBelow);
    m_speedSlider->setTickInterval(1);
    tl->addWidget(m_speedSlider);
    m_speedLabel = new QLabel("Slow", m_toolbar);
    m_speedLabel->setStyleSheet("color:#ccc; font-size:9pt;");
    m_speedLabel->setMinimumWidth(34);
    tl->addWidget(m_speedLabel);

    auto* pl = new QLabel("Palette:", m_toolbar);
    pl->setStyleSheet("color: #aaa; font-size: 9pt;");
    tl->addWidget(pl);
    m_paletteCombo = new QComboBox(m_toolbar);
    m_paletteCombo->addItems({"Earth","Classic","Greyscale","Night"});
    m_paletteCombo->setCurrentIndex(0);
    m_paletteCombo->setMaximumWidth(86);
    m_paletteCombo->setStyleSheet(
        "background:#1a1a2e; color:#ccc; border:1px solid #333;");
    tl->addWidget(m_paletteCombo);

    auto* ll = new QLabel("Level:", m_toolbar);
    ll->setStyleSheet("color: #888; font-size: 9pt;");
    tl->addWidget(ll);
    m_levelSpinBox = new QSpinBox(m_toolbar);
    m_levelSpinBox->setRange(-140, 0);
    m_levelSpinBox->setValue(-60);
    m_levelSpinBox->setSuffix(" dB");
    m_levelSpinBox->setSingleStep(5);
    m_levelSpinBox->setFixedWidth(76);
    m_levelSpinBox->setStyleSheet(
        "background:#1a1a2e; color:#ccc; border:1px solid #333;");
    m_levelSpinBox->setToolTip(
        "Waterfall floor level in dBFS\n"
        "Lower = more sensitive\n"
        "Higher = less clutter\n"
        "Does not affect decoding");
    tl->addWidget(m_levelSpinBox);

    m_afcLabel = new QLabel("AFC: --", m_toolbar);
    m_afcLabel->setStyleSheet(
        "color:#555; font-family:'Courier New'; font-size:9pt;");
    m_afcLabel->setMinimumWidth(110);
    tl->addWidget(m_afcLabel);

    tl->addStretch();

    auto* hint = new QLabel("Right-click to tune", m_toolbar);
    hint->setStyleSheet("color:#555; font-size:8pt;");
    tl->addWidget(hint);

    outerLayout->addWidget(m_toolbar);

    // ── Display area ──────────────────────────────────────────────────────
    m_displayArea = new QWidget(this);
    m_displayArea->setMinimumHeight(60);
    m_displayArea->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_displayArea->setMouseTracking(true);
    m_displayArea->installEventFilter(this);
    outerLayout->addWidget(m_displayArea, 1);

    m_image = QImage(m_imageWidth, WATERFALL_ROWS, QImage::Format_RGB32);
    m_image.fill(qRgb(0, 0, 8));

    // Load saved settings
    QSettings s;
    int ri = s.value("waterfall/rangeIndex",   3).toInt();
    int si = s.value("waterfall/speedIndex",   0).toInt();
    int pi = s.value("waterfall/paletteIndex", 0).toInt();
    int li = s.value("waterfall/floorDb",    -60).toInt();
    m_rangeCombo->setCurrentIndex(std::clamp(ri, 0, 5));
    m_speedSlider->setValue(std::clamp(si, 0, 3));
    m_paletteCombo->setCurrentIndex(std::clamp(pi, 0, 3));
    m_levelSpinBox->setValue(std::clamp(li, -140, 0));
    onRangeChanged(m_rangeCombo->currentIndex());
    onSpeedChanged(m_speedSlider->value());
    onPaletteChanged(m_paletteCombo->currentIndex());
    setFloorDb(static_cast<float>(m_levelSpinBox->value()));

    connect(m_rangeCombo,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &WaterfallWidget::onRangeChanged);
    connect(m_speedSlider, &QSlider::valueChanged,
            this, &WaterfallWidget::onSpeedChanged);
    connect(m_paletteCombo,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &WaterfallWidget::onPaletteChanged);
    connect(m_levelSpinBox,
            QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int val) {
                setFloorDb(static_cast<float>(val));
                QSettings().setValue("waterfall/floorDb", val);
            });
}

WaterfallWidget::~WaterfallWidget() {
    if (m_fftCfg) kiss_fft_free(m_fftCfg);
}

void WaterfallWidget::onRangeChanged(int index) {
    static const float ranges[] = {
        1500.f,2000.f,2500.f,3000.f,3500.f,4000.f };
    if (index >= 0 && index < 6) m_hzMax = ranges[index];
    QSettings().setValue("waterfall/rangeIndex", index);
    if (m_displayArea) m_displayArea->update();
}

void WaterfallWidget::onSpeedChanged(int value) {
    static const int   skips[]  = {8, 4, 2, 1};
    static const char* labels[] = {"Slow","2x","4x","Fast"};
    int v = std::clamp(value, 0, 3);
    m_chunkSkip = skips[v];
    m_speedLabel->setText(labels[v]);
    QSettings().setValue("waterfall/speedIndex", value);
}

void WaterfallWidget::onPaletteChanged(int index) {
    switch (index) {
    case 0: buildEarthPalette(m_palette);     break;
    case 1: buildClassicPalette(m_palette);   break;
    case 2: buildGreyscalePalette(m_palette); break;
    case 3: buildNightPalette(m_palette);     break;
    default: buildEarthPalette(m_palette);    break;
    }
    QSettings().setValue("waterfall/paletteIndex", index);
    m_image.fill(qRgb(0, 0, 8));
    m_rowCount = 0;
    if (m_displayArea) m_displayArea->update();
}

void WaterfallWidget::buildPalettes() {
    onPaletteChanged(m_paletteCombo->currentIndex());
}

void WaterfallWidget::setAfcOffset(float hz) {
    m_afcOffsetHz = hz;
    if (std::abs(hz) < 0.5f) {
        m_afcLabel->setText("AFC: --");
        m_afcLabel->setStyleSheet(
            "color:#555; font-family:'Courier New'; font-size:9pt;");
    } else {
        QString arrow = (hz > 0.0f) ? " ↑" : " ↓";
        m_afcLabel->setText(
            QString("AFC: %1%2 Hz%3")
            .arg(hz > 0 ? "+" : "")
            .arg(hz, 0, 'f', 1)
            .arg(arrow));
        bool atLimit = std::abs(hz) >= 70.0f;
        m_afcLabel->setStyleSheet(
            atLimit
            ? "color:#FF6B35; font-family:'Courier New'; font-size:9pt;"
            : "color:#6DB640; font-family:'Courier New'; font-size:9pt;");
    }
    if (m_displayArea) m_displayArea->update();
}

std::vector<float> WaterfallWidget::computeFFT(
    const std::vector<float>& samples)
{
    m_overlapBuffer.insert(m_overlapBuffer.end(),
                           samples.begin(), samples.end());
    if (static_cast<int>(m_overlapBuffer.size()) < FFT_SIZE) return {};

    std::vector<kiss_fft_cpx> in(FFT_SIZE), out(FFT_SIZE);
    std::vector<float> result;

    // Drain all complete frames; return the most recent for display.
    // With 50% overlap (HOP_SIZE) m_chunkSkip>1 accumulates multiple hops —
    // processing them all here keeps the buffer bounded.
    while (static_cast<int>(m_overlapBuffer.size()) >= FFT_SIZE) {
        // Hann-windowed complex input (imaginary = 0 for real signal)
        for (int i = 0; i < FFT_SIZE; i++) {
            float w = 0.5f * (1.0f - std::cos(
                2.0f * static_cast<float>(M_PI) * i / (FFT_SIZE - 1)));
            in[i].r = m_overlapBuffer[i] * w;
            in[i].i = 0.0f;
        }

        kiss_fft(m_fftCfg, in.data(), out.data());

        // Only positive frequencies (first FFT_SIZE/2 bins)
        int nBins = FFT_SIZE / 2;
        result.resize(nBins);
        for (int i = 0; i < nBins; i++) {
            float mag = std::sqrt(out[i].r * out[i].r + out[i].i * out[i].i);
            result[i] = 20.0f * std::log10(mag + 1e-10f);
        }

        // 50% overlap — keep second half for next frame
        m_overlapBuffer.erase(
            m_overlapBuffer.begin(),
            m_overlapBuffer.begin() + HOP_SIZE);
    }

    return result;
}

void WaterfallWidget::pushChunk(const std::vector<float>& samples) {
    m_chunkCount++;
    if (m_chunkCount < m_chunkSkip) return;
    m_chunkCount = 0;

    auto db = computeFFT(samples);
    if (db.empty()) return;
    addRow(db);
    if (m_displayArea) m_displayArea->update();
}

void WaterfallWidget::addRow(const std::vector<float>& magnitudesDb) {
    int w = m_displayArea ? m_displayArea->width() : m_imageWidth;
    if (w < 1) w = m_imageWidth;

    if (w != m_imageWidth) {
        m_imageWidth = w;
        m_image = QImage(m_imageWidth, WATERFALL_ROWS, QImage::Format_RGB32);
        m_image.fill(qRgb(0, 0, 8));
        m_rowCount = 0;
    }

    int nBins = static_cast<int>(magnitudesDb.size());

    // Scroll: copy rows 0..N-2 → 1..N-1 (newest at top = index 0)
    for (int row = WATERFALL_ROWS - 1; row > 0; row--) {
        QRgb* dst = reinterpret_cast<QRgb*>(m_image.scanLine(row));
        const QRgb* src = reinterpret_cast<const QRgb*>(
            m_image.scanLine(row - 1));
        std::memcpy(dst, src, m_imageWidth * sizeof(QRgb));
    }

    // Paint new row at top — linear interpolation between adjacent bins
    QRgb* row0 = reinterpret_cast<QRgb*>(m_image.scanLine(0));
    for (int x = 0; x < m_imageWidth; x++) {
        float hz      = static_cast<float>(x) / m_imageWidth * m_hzMax;
        float exactBin = hz / m_binWidth;
        int   bin0    = static_cast<int>(exactBin);
        int   bin1    = bin0 + 1;
        bin0 = std::max(0, std::min(nBins - 1, bin0));
        bin1 = std::max(0, std::min(nBins - 1, bin1));
        float frac = exactBin - static_cast<float>(bin0);
        float db   = magnitudesDb[bin0] * (1.0f - frac)
                   + magnitudesDb[bin1] * frac;
        row0[x] = dbToColor(db);
    }

    if (m_rowCount < WATERFALL_ROWS) m_rowCount++;
}

QRgb WaterfallWidget::dbToColor(float db) const {
    float t = (db - m_floorDb) / m_rangeDb;
    t = std::max(0.0f, std::min(1.0f, t));
    return m_palette[static_cast<int>(t * 255.0f)];
}

float WaterfallWidget::pixelToHz(int x) const {
    int w = m_displayArea ? m_displayArea->width() : 1;
    if (w < 1 || m_hzMax <= 0) return 0.0f;
    return static_cast<float>(x) / static_cast<float>(w) * m_hzMax;
}

int WaterfallWidget::hzToPixel(float hz) const {
    int w = m_displayArea ? m_displayArea->width() : 1;
    if (m_hzMax <= 0) return 0;
    return static_cast<int>(hz / m_hzMax * static_cast<float>(w));
}

void WaterfallWidget::drawOverlays(QPainter& p, int w, int h) const {
    // ── Fixed passband markers (gray dashed) ──────────────────────────────
    // These NEVER move. They show where the signal SHOULD be.
    float loHz = static_cast<float>(HavenFSK::BASE_FREQ);
    float hiHz = static_cast<float>(HavenFSK::BASE_FREQ)
               + static_cast<float>(HavenFSK::NUM_TONES)
               * static_cast<float>(HavenFSK::SYMBOL_RATE);

    QPen grayPen(QColor(0xB8, 0xB8, 0xB8, 204), 1.2f, Qt::DashLine);
    p.setPen(grayPen);

    int loX = hzToPixel(loHz);
    int hiX = hzToPixel(hiHz);

    if (loX >= 0 && loX < w) {
        p.drawLine(loX, 0, loX, h);
        p.setPen(QColor(0xB8, 0xB8, 0xB8, 180));
        p.setFont(QFont("Courier New", 8));
        p.drawText(loX + 2, 14, QString("%1").arg(static_cast<int>(loHz)));
        p.setPen(grayPen);
    }
    if (hiX >= 0 && hiX < w) {
        p.drawLine(hiX, 0, hiX, h);
        p.setPen(QColor(0xB8, 0xB8, 0xB8, 180));
        p.drawText(hiX + 2, 14, QString("%1").arg(static_cast<int>(hiHz)));
        p.setPen(grayPen);
    }

    // ── AFC tracking lines (soft green solid) ─────────────────────────────
    // Float with afcOffsetHz. Color #6DB640 chosen for palette contrast.
    if (std::abs(m_afcOffsetHz) >= 0.5f) {
        float afcLo = loHz + m_afcOffsetHz;
        float afcHi = hiHz + m_afcOffsetHz;
        QPen greenPen(QColor(0x6D, 0xB6, 0x40, 230), 1.5f, Qt::SolidLine);
        p.setPen(greenPen);

        int afcLoX = hzToPixel(afcLo);
        int afcHiX = hzToPixel(afcHi);

        if (afcLoX >= 0 && afcLoX < w)
            p.drawLine(afcLoX, 0, afcLoX, h);
        if (afcHiX >= 0 && afcHiX < w)
            p.drawLine(afcHiX, 0, afcHiX, h);
    }

    // ── Tuning line (bright green, only when in tuning mode) ──────────────
    if (m_tuningActive && m_tuningPixel >= 0 && m_tuningPixel < w) {
        p.setPen(QPen(QColor(80, 255, 140, 220), 1.0f));
        p.drawLine(m_tuningPixel, 0, m_tuningPixel, h);
        float hz = pixelToHz(m_tuningPixel);
        p.setPen(QColor(80, 255, 140, 220));
        p.setFont(QFont("Courier New", 8));
        p.drawText(m_tuningPixel + 3, h - 6,
                    QString("%1 Hz").arg(static_cast<int>(hz)));
    }

    // ── Frequency axis ticks ──────────────────────────────────────────────
    p.setPen(QColor(160, 160, 160, 140));
    p.setFont(QFont("Courier New", 7));
    for (int hz = 0; hz <= static_cast<int>(m_hzMax); hz += 500) {
        int x = hzToPixel(static_cast<float>(hz));
        if (x >= 0 && x < w) {
            p.drawLine(x, h - 8, x, h);
            if (hz > 0)
                p.drawText(x - 14, h - 10, QString::number(hz));
        }
    }
}

bool WaterfallWidget::eventFilter(QObject* obj, QEvent* event) {
    if (obj != m_displayArea) return false;

    if (event->type() == QEvent::Paint) {
        QPainter painter(m_displayArea);
        int w = m_displayArea->width();
        int h = m_displayArea->height();
        painter.drawImage(
            QRect(0, 0, w, h),
            m_image,
            QRect(0, 0, m_imageWidth, WATERFALL_ROWS));
        drawOverlays(painter, w, h);
        return true;
    }

    if (event->type() == QEvent::MouseButtonPress) {
        auto* me = static_cast<QMouseEvent*>(event);
        if (me->button() == Qt::RightButton) {
            m_tuningMode   = true;
            m_tuningActive = true;
            m_tuningPixel  = me->pos().x();
            emit tuningLineAt(pixelToHz(m_tuningPixel));
            m_displayArea->update();
            return true;
        }
        if (me->button() == Qt::LeftButton && m_tuningMode) {
            float hz = pixelToHz(m_tuningPixel);
            m_tuningMode   = false;
            m_tuningActive = false;
            emit tuneRequested(hz);
            m_displayArea->update();
            return true;
        }
    }

    if (event->type() == QEvent::MouseMove && m_tuningMode) {
        auto* me = static_cast<QMouseEvent*>(event);
        m_tuningPixel = me->pos().x();
        emit tuningLineAt(pixelToHz(m_tuningPixel));
        m_displayArea->update();
        return true;
    }

    if (event->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(event);
        if (ke->key() == Qt::Key_Escape && m_tuningMode) {
            m_tuningMode   = false;
            m_tuningActive = false;
            m_displayArea->update();
            return true;
        }
    }

    return false;
}

void WaterfallWidget::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Escape && m_tuningMode) {
        m_tuningMode   = false;
        m_tuningActive = false;
        if (m_displayArea) m_displayArea->update();
    }
}

void WaterfallWidget::resizeEvent(QResizeEvent*) {
    if (m_displayArea) m_displayArea->update();
}
