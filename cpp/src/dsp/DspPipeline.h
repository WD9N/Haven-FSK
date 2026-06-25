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
    Idle,       // no signal, not buffering
    Buffering,  // DCD active, accumulating raw audio
    Decoding,   // processing completed buffer
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

    // Raw audio buffer — accumulates samples while DCD active.
    // Processed in one pass when DCD drops.
    std::vector<float> m_rxBuffer;
    bool               m_buffering {false};

    // 60-second safety limit (~11 MB) — prevents runaway on continuous carrier
    static constexpr int MAX_BUFFER_SAMPLES = SAMPLE_RATE * 60;

    // ── RX gain ───────────────────────────────────────────────────────────
    float m_rxGain {1.0f};

    // ── TX state ──────────────────────────────────────────────────────────
    QString m_lastTxText;

    // ── AFC state ─────────────────────────────────────────────────────────
    float  m_afcOffsetHz   = 0.0f;
    bool   m_afcEnabled    = true;
    float  m_afcPhase      = 0.0f;

    static constexpr float AFC_TRACK_ALPHA = 0.02f;
    static constexpr float AFC_RESET_DECAY = 0.50f;

    void  applyAfcCorrection(std::vector<float>& samples);
    float measureToneOffset(
        const std::vector<std::vector<float>>& softSymbols) const;
    void  updateAfcTracking(
        const std::vector<std::vector<float>>& softSymbols);

    // ── RS measurement cache ──────────────────────────────────────────────
    QMap<QString, RxMeasurement> m_rxCache;
    static constexpr int RS_CACHE_MINUTES = 10;

    void updateRxCache(const RxMessage& msg);
    void expireRxCache();

    // ── Helpers ───────────────────────────────────────────────────────────
    void processRxBuffer();
    void processFrame(const std::vector<std::vector<float>>& softSymbols);
    void setRxState(RxState newState);
    void resetRx();
};

} // namespace HavenFSK
