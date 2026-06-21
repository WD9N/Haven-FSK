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
// after each successful decode. Used to generate RS reports.
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
    // Assemble and queue a message for transmission.
    // Emits txAudioReady() with the complete frame audio.
    // Returns false if a transmission is already in progress.
    bool transmit(const QString& text);

    // ── State ─────────────────────────────────────────────────────────────
    RxState rxState()       const { return m_rxState; }
    bool    dcdActive()     const { return m_dcdActive; }
    bool    isTransmitting() const { return m_transmitting; }

    // ── RS measurement cache ──────────────────────────────────────────────
    // Retrieve cached signal measurement for a specific callsign.
    // Returns nullptr if no entry exists or entry has expired (>10 min).
    const RxMeasurement* getRxMeasurement(const QString& callsign) const;

    // Compute RS report string from a measurement.
    // R: 1-5 from FEC convergence/iterations
    // S: 1-9 from SNR dB
    // Returns formatted string e.g. "57"
    static QString computeRS(const RxMeasurement& m);

    // Extract sender callsign from decoded message text.
    // Looks for standard "THEIRCALL DE MYCALL" pattern.
    // Returns empty string if not found.
    static QString parseSenderCallsign(const QString& text,
                                       const QString& myCallsign);

public slots:
    // Connected to AudioEngine::rxDataReady
    void onAudioChunk(const std::vector<float>& samples);

    // Connected to AudioEngine::txComplete
    void onTxComplete();

signals:
    // Emitted when a complete message is decoded
    void messageReceived(const HavenFSK::RxMessage& msg);

    // Emitted when DCD state changes — for UI carrier indicator
    void dcdChanged(bool active);

    // Emitted when RX state changes — for UI status display
    void rxStateChanged(HavenFSK::RxState state);

    // Emitted with complete TX audio ready for AudioEngine::startTx()
    void txAudioReady(const std::vector<float>& samples);

    // Emitted when preamble is detected — for UI sync indicator
    void preambleDetected(float score);

    // Emitted periodically with symbol count during reception
    void rxProgress(int symbolsReceived, int symbolsExpected);

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

    // Sample accumulator — collects partial chunks until a full symbol
    // worth of samples is available
    std::vector<float> m_sampleAccum;

    // Soft symbol accumulator — collects symbols after preamble detection
    std::vector<std::vector<float>> m_symbolAccum;

    // Preamble search window — soft symbols collected during Searching state
    std::vector<std::vector<float>> m_searchWindow;

    // Expected frame size in symbols (set when preamble detected)
    // Set to a safe maximum initially, refined after header decode
    int m_expectedSymbols = 0;

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

    // Maximum symbols to accumulate before giving up on a frame
    // = PREAMBLE_LENGTH + HEADER_SYMS + CRC_SYMS + MAX_BLOCKS * 48
    // Use 32 FEC blocks as a generous maximum
    static constexpr int MAX_FRAME_SYMBOLS =
        16 + 4 + 4 + (32 * 48);  // 1560 symbols = ~50 seconds
};

} // namespace HavenFSK
