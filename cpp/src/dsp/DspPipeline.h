#pragma once
#include <QObject>
#include <QString>
#include <QMap>
#include <QDateTime>
#include <vector>
#include <memory>
#include "Constants.h"
#include "Modulator.h"
#include "Demodulator.h"
#include "DCD.h"
#include "Preamble.h"
#include "Frame.h"

namespace HavenFSK {

// RX state machine states
enum class RxState {
    Idle,       // rolling buffer active, continuously scanning for preamble
    Collecting, // preamble found, waiting for complete frame
};

// Result of a received message, emitted to the UI layer
struct RxMessage {
    QString text;            // decoded text
    bool    crcOk;           // CRC verified
    bool    converged;       // FEC converged
    int     nBlocks;         // FEC blocks decoded
    float   snr;             // estimated SNR in dB (from DCD)
    int     fecIterations;   // actual BP iterations used
    QString senderCallsign;  // callsign parsed from decoded text
};

// Per-station signal quality measurement stored in cache
struct RxMeasurement {
    float     snrDb;
    int       fecIterations;
    bool      converged;
    QDateTime timestamp;
};

class DspPipeline : public QObject
{
    Q_OBJECT

public:
    explicit DspPipeline(QObject* parent = nullptr);
    ~DspPipeline() override = default;

    // ── TX ────────────────────────────────────────────────────────────────
    bool transmit(const QString& text);

    // ── State ─────────────────────────────────────────────────────────────
    RxState rxState()        const { return m_rxState; }
    bool    dcdActive()      const { return m_dcdActive; }
    bool    isTransmitting() const { return m_transmitting; }

    // ── RX gain ───────────────────────────────────────────────────────────
    void setRxGain(float linear) { m_rxGain = linear; }

    // ── AFC controls ──────────────────────────────────────────────────────
    void  setAfcEnabled(bool enabled) { m_afcEnabled = enabled; }
    bool  afcEnabled()      const     { return m_afcEnabled; }
    float afcOffsetHz()     const     { return m_afcOffsetHz; }

    bool lastTxWasCQ() const {
        return m_lastTxText.contains("CQ", Qt::CaseInsensitive);
    }

    // AFC range — beyond this AFC clamps and MainWindow warns operator
    static constexpr float AFC_MAX_HZ = 75.0f;

    // ── RS measurement cache ──────────────────────────────────────────────
    const RxMeasurement* getRxMeasurement(const QString& callsign) const;
    static QString computeRS(const RxMeasurement& m);
    static QString parseSenderCallsign(const QString& text,
                                       const QString& myCallsign);

public slots:
    void onAudioChunk(const std::vector<float>& samples);
    void onTxComplete();

signals:
    void messageReceived(const HavenFSK::RxMessage& msg);
    void dcdChanged(bool active);
    void rxStateChanged(HavenFSK::RxState state);
    void txAudioReady(const std::vector<float>& samples);
    void preambleDetected(float score);
    void rxProgress(int symbolsReceived, int symbolsExpected);
    void afcOffsetChanged(float hz);
    void messageTransmitted(const QString& text);

private:
    // ── DSP objects ───────────────────────────────────────────────────────
    Modulator   m_modulator;
    Demodulator m_demodulator;
    DCD         m_dcd;
    Preamble    m_preamble;
    Frame       m_frame;

    // ── RX state ──────────────────────────────────────────────────────────
    RxState m_rxState      = RxState::Idle;
    bool    m_dcdActive    = false;
    bool    m_transmitting = false;

    // Snapshot buffer — set to the rolling buffer contents when preamble
    // is found; grows as new audio arrives during Collecting.
    std::vector<float> m_rxBuffer;

    // Rolling audio buffer: last PRE_TRIGGER_SAMPLES kept while Idle.
    // Snapshotted into m_rxBuffer the moment preamble is detected so the
    // full preamble audio is available for frame assembly.
    std::vector<float> m_preTrigger;

    // 30-second safety limit — prevents runaway on continuous carrier
    static constexpr int MAX_BUFFER_SAMPLES = SAMPLE_RATE * 30;

    // ── Preamble-triggered decode state ───────────────────────────────────
    int   m_timingOffset    = 0;   // best sample offset from timing sweep
    int   m_preambleSymOff  = -1;  // symbol index of preamble in buffer
    int   m_nBlocks         = -1;  // nBlocks from header, -1 until decoded
    int   m_symsNeeded      = 0;   // total post-preamble symbols for full frame
    int   m_demodBinOffset  = 0;   // FFT bin offset for this frame's demodulation
                                   // (from hypothesis scan + residual; not persisted when AFC disabled)
    int   m_scanTicks       = 0;   // chunks elapsed, used for scan interval
    int   m_collectTicks    = 0;   // chunks elapsed in Collecting state
    int   m_lastCheckSamples = 0;  // buffer size at last tryCompleteFrame call

    // Preamble scan runs every N chunks while Idle (≈340 ms at 2048/48000)
    static constexpr int SCAN_INTERVAL_CHUNKS = 8;

    // Give up collecting if frame not complete within this many chunks (≈20 s)
    static constexpr int COLLECT_TIMEOUT_CHUNKS =
        static_cast<int>(SAMPLE_RATE * 20.0 / AUDIO_CHUNK_SAMPLES);

    // Coarse preamble score that triggers the full timing sweep (cheap filter)
    static constexpr float COARSE_PREAMBLE_THRESHOLD = 0.18f;

    // Soft preamble correlation threshold. Score range: 0.0625 = uniform noise
    // (1/NUM_TONES per symbol), 1.0 = perfect. Set at 0.30 — well above the
    // ~0.225 ceiling for a constant 500-Hz carrier false positive.
    static constexpr float SOFT_PREAMBLE_THRESHOLD = 0.30f;

    // Rolling buffer depth while Idle — 3 s ensures the full preamble
    // is captured even if detection happens after preamble ends.
    static constexpr int PRE_TRIGGER_SAMPLES = SAMPLE_RATE * 3;  // 3 s

    // ── RX gain ───────────────────────────────────────────────────────────
    float m_rxGain {1.0f};

    // ── TX state ──────────────────────────────────────────────────────────
    QString m_lastTxText;

    // ── AFC state ─────────────────────────────────────────────────────────
    float  m_afcOffsetHz   = 0.0f;
    bool   m_afcEnabled    = false;

    float measureToneOffset(
        const std::vector<std::vector<float>>& softSymbols) const;

    // ── RS measurement cache ──────────────────────────────────────────────
    QMap<QString, RxMeasurement> m_rxCache;
    static constexpr int RS_CACHE_MINUTES = 10;

    void updateRxCache(const RxMessage& msg);
    void expireRxCache();

    // ── Helpers ───────────────────────────────────────────────────────────

    // Run coarse+fine preamble search on m_rxBuffer.
    // On success sets m_timingOffset, m_preambleSymOff, enters Collecting.
    void tryFindPreamble();

    // Check whether m_rxBuffer now contains a complete frame.
    // Decodes header on first call (sets m_nBlocks, m_symsNeeded),
    // then decodes the full frame once enough symbols are present.
    void tryCompleteFrame();

    void processFrame(const std::vector<std::vector<float>>& softSymbols);
    void setRxState(RxState newState);
    void resetRx();
};

} // namespace HavenFSK
