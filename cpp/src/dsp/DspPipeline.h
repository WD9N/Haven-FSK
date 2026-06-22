#pragma once
#include <QObject>
#include <QString>
#include <QMap>
#include <QDateTime>
#include <vector>
#include <deque>
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
    Idle,           // DCD not active, discarding audio
    Searching,      // DCD active, looking for preamble
    Receiving,      // Preamble found, accumulating frame symbols
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
    float     snrDb;         // measured SNR from DCD band energy
    int       fecIterations; // BP iterations — lower = cleaner signal
    bool      converged;     // FEC converged within iteration limit
    QDateTime timestamp;     // when this measurement was taken
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

    // ── AFC controls ──────────────────────────────────────────────────────
    void  setAfcEnabled(bool enabled) { m_afcEnabled = enabled; }
    bool  afcEnabled()      const     { return m_afcEnabled; }
    float afcOffsetHz()     const     { return m_afcOffsetHz; }

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

    // Emitted when AFC offset changes — connected to WaterfallWidget
    void afcOffsetChanged(float hz);

    // Emitted just before transmission begins — connected to RxDisplay
    void messageTransmitted(const QString& text);

private:
    // ── DSP objects ───────────────────────────────────────────────────────
    Modulator   m_modulator;
    Demodulator m_demodulator;
    DCD         m_dcd;
    Preamble    m_preamble;
    Frame       m_frame;

    // ── RX state ──────────────────────────────────────────────────────────
    RxState m_rxState    = RxState::Idle;
    bool    m_dcdActive  = false;
    bool    m_transmitting = false;

    std::vector<float> m_sampleAccum;
    std::vector<std::vector<float>> m_symbolAccum;
    std::vector<std::vector<float>> m_searchWindow;
    int m_expectedSymbols = 0;

    // ── AFC state ─────────────────────────────────────────────────────────
    float  m_afcOffsetHz   = 0.0f;
    bool   m_afcEnabled    = true;
    float  m_afcPhase      = 0.0f;   // NCO phase accumulator

    static constexpr float AFC_TRACK_ALPHA = 0.02f;  // slow tracking
    static constexpr float AFC_RESET_DECAY = 0.50f;  // partial reset

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
    void processSymbol(const std::vector<float>& softEnergies);
    void processFrame();
    void setRxState(RxState newState);
    void resetRx();

    static constexpr int MAX_FRAME_SYMBOLS =
        16 + 4 + 4 + (32 * 48);  // 1560 symbols = ~50 seconds
};

} // namespace HavenFSK
