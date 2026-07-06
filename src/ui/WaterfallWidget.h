#pragma once
#include <QWidget>
#include <QImage>
#include <QComboBox>
#include <QSlider>
#include <QSpinBox>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <vector>
#include <cstdint>
#include "../dsp/Constants.h"
// TODO(Phase 5): passband markers should come from IModem::passbandLowHz()/
// HighHz() via DspPipeline, not compile-time MFSK constants. MfskConstants.h
// is a temporary bridge so this keeps compiling until that lands.
#include "../dsp/MfskConstants.h"
#include "kiss_fft.h"

// WaterfallWidget — scrolling spectrogram display for HAVEN-FSK.
//
// IMPORTANT: pushChunk() must receive RAW audio directly from
// AudioEngine::rxDataReady — never AFC-corrected audio. The waterfall
// always shows the true received signal so the operator can see actual
// signal positions regardless of AFC activity.
//
// Two sets of vertical markers:
//   Gray dashed (#B8B8B8) — the active mode's fixed passband (see
//     setPassband()). Defaults to MFSK-16's passband; updated on mode
//     change. Does not move within a session except via setPassband().
//   Soft green solid (#6DB640) — AFC tracking lines, float with offset.
//     Only visible when |afcOffset| >= 0.5 Hz.
//
// Four color palettes: Earth (default), Classic, Greyscale, Night.
// Four speeds: Slow (default, ~3 rows/sec) through Fast (~23 rows/sec).
// Settings persisted in QSettings.
//
// Tuning: right-click → movable line, left-click → confirm,
//         Escape → cancel. Emits tuneRequested(audioHz) on confirm.

class WaterfallWidget : public QWidget
{
    Q_OBJECT
public:
    explicit WaterfallWidget(QWidget* parent = nullptr);
    ~WaterfallWidget() override;

    // Feed raw audio chunk for FFT and display.
    // MUST be raw audio from AudioEngine — never AFC-corrected.
    void pushChunk(const std::vector<float>& samples);

    // Update AFC offset for floating green marker lines.
    void setAfcOffset(float hz);

    // Update the fixed (gray dashed) passband markers to match the
    // active mode. Defaults to MFSK-16's passband; call after a mode
    // change (see MainWindow::onModeChanged()) so the waterfall doesn't
    // keep showing the previous mode's passband.
    void setPassband(float loHz, float hiHz) {
        m_passbandLoHz = loHz;
        m_passbandHiHz = hiHz;
    }

    // Adjust the dBFS floor for color mapping (display only).
    // Lower = more sensitive. Range: -140 to 0.
    void setFloorDb(float db) {
        m_floorDb = std::max(-140.0f, std::min(0.0f, db));
    }

signals:
    // Emitted when operator left-clicks after right-click tuning.
    // audioHz: the audio frequency clicked.
    // Caller: newDialHz = currentDialHz + (audioHz - BASE_FREQ)
    void tuneRequested(float audioHz);

    // Emitted as tuning line moves — for status bar real-time display.
    void tuningLineAt(float audioHz);

private slots:
    void onSpeedChanged(int value);
    void onRangeChanged(int index);
    void onPaletteChanged(int index);

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void buildPalettes();
    void addRow(const std::vector<float>& magnitudesDb);
    float pixelToHz(int x) const;
    int   hzToPixel(float hz) const;
    std::vector<float> computeFFT(const std::vector<float>& samples);
    QRgb  dbToColor(float db) const;
    void  drawOverlays(QPainter& p, int w, int h) const;

    static constexpr int   WATERFALL_ROWS = 120;
    static constexpr int   FFT_SIZE       = 4096;  // 11.7 Hz/bin at 48kHz
    static constexpr int   HOP_SIZE       = FFT_SIZE / 2;  // 50% overlap

    // Display state
    QImage  m_image;
    int     m_imageWidth = 512;
    int     m_rowCount   = 0;

    // Speed
    int     m_chunkSkip  = 8;   // chunks between rows (Slow default)
    int     m_chunkCount = 0;

    // Frequency range
    float   m_hzMax      = 3000.0f;

    // FFT overlap buffer — HOP_SIZE new samples per frame, FFT_SIZE window
    std::vector<float> m_overlapBuffer;

    // Persistent FFT config — allocated once, freed in destructor
    kiss_fft_cfg m_fftCfg {nullptr};

    // Hz per FFT bin — precomputed, used in addRow()
    float m_binWidth {static_cast<float>(HavenFSK::SAMPLE_RATE)
                      / static_cast<float>(FFT_SIZE)};

    // Color palette and level
    QRgb    m_palette[256];
    float   m_floorDb  = -60.0f;  // dBFS floor for color mapping
    float   m_rangeDb  =  80.0f;  // dynamic range (fixed)

    // AFC offset — drives floating green marker lines
    float   m_afcOffsetHz = 0.0f;

    // Fixed passband markers — defaults to MFSK-16's passband;
    // MainWindow::onModeChanged() updates these via setPassband() when
    // the operator switches modes.
    float   m_passbandLoHz {static_cast<float>(HavenFSK::BASE_FREQ)};
    float   m_passbandHiHz {static_cast<float>(HavenFSK::BASE_FREQ)
                            + static_cast<float>(HavenFSK::NUM_TONES)
                            * static_cast<float>(HavenFSK::SYMBOL_RATE)};

    // Tuning line state
    bool    m_tuningMode   = false;
    int     m_tuningPixel  = -1;
    bool    m_tuningActive = false;

    // Toolbar controls
    QWidget*   m_toolbar      {nullptr};
    QWidget*   m_displayArea  {nullptr};
    QComboBox* m_rangeCombo   {nullptr};
    QSlider*   m_speedSlider  {nullptr};
    QLabel*    m_speedLabel   {nullptr};
    QComboBox* m_paletteCombo {nullptr};
    QSpinBox*  m_levelSpinBox {nullptr};
    QLabel*    m_afcLabel     {nullptr};
};
