#pragma once
#include <QWidget>
#include <QImage>
#include <QComboBox>
#include <QSlider>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <vector>
#include <cstdint>
#include "../dsp/Constants.h"

// WaterfallWidget — scrolling spectrogram display for HAVEN-FSK.
//
// IMPORTANT: pushChunk() must receive RAW audio directly from
// AudioEngine::rxDataReady — never AFC-corrected audio. The waterfall
// always shows the true received signal so the operator can see actual
// signal positions regardless of AFC activity.
//
// Two sets of vertical markers:
//   Gray dashed (#B8B8B8) — fixed passband at BASE_FREQ and
//     BASE_FREQ + NUM_TONES * SYMBOL_RATE. Never move.
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

    // Feed raw audio chunk for FFT and display.
    // MUST be raw audio from AudioEngine — never AFC-corrected.
    void pushChunk(const std::vector<float>& samples);

    // Update AFC offset for floating green marker lines.
    void setAfcOffset(float hz);

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
    static constexpr int   FFT_SIZE       = 2048;
    static constexpr float DB_MIN         = -60.0f;
    static constexpr float DB_MAX         =  40.0f;

    // Display state
    QImage  m_image;
    int     m_imageWidth = 512;
    int     m_rowCount   = 0;

    // Speed
    int     m_chunkSkip  = 8;   // chunks between rows (Slow default)
    int     m_chunkCount = 0;

    // Frequency range
    float   m_hzMax      = 3000.0f;

    // FFT accumulation across chunk boundaries
    std::vector<float> m_fftAccum;

    // Color palette
    QRgb    m_palette[256];

    // AFC offset — drives floating green marker lines
    float   m_afcOffsetHz = 0.0f;

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
    QLabel*    m_afcLabel     {nullptr};
};
