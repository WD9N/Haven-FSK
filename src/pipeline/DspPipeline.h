#pragma once
#include <QObject>
#include <QString>
#include <QMap>
#include <QDateTime>
#include <vector>
#include <memory>
#include <atomic>
#include <mutex>
#include <optional>
#include "../dsp/Constants.h"
#include "../dsp/IModem.h"

namespace HavenFSK {

// RX state machine states — mirrors IModem's ModemRxState, kept as a
// distinct Qt-visible enum since it's part of DspPipeline's existing
// public signal surface (rxStateChanged) consumed by the UI layer.
enum class RxState {
    Idle,       // rolling buffer active, continuously scanning for preamble
    Collecting, // preamble found, waiting for complete frame
};

// Result of a received message, emitted to the UI layer
struct RxMessage {
    QString text;            // decoded text
    bool    crcOk;           // CRC verified (MFSK only)
    bool    converged;       // FEC converged (MFSK only)
    int     nBlocks;         // FEC blocks decoded (MFSK only)
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

// DspPipeline — Qt-facing RX/TX glue between AudioEngine and the active
// IModem implementation. Owns mode-agnostic bookkeeping (RX gain, RS
// measurement cache keyed on decoded text, TX/transmitting state) and
// translates IModem's Qt-free ModemRxEvent stream into Qt signals for
// the UI layer. Mode-specific DSP logic (state machine, AFC, framing)
// lives behind IModem — see MfskModem for the 16-tone MFSK implementation
// and ModemFactory::createModem() for mode selection.
class DspPipeline : public QObject
{
    Q_OBJECT

public:
    explicit DspPipeline(QObject* parent = nullptr);
    ~DspPipeline() override = default;

    // ── TX ────────────────────────────────────────────────────────────────
    bool transmit(const QString& text);

    // ── Mode ──────────────────────────────────────────────────────────────
    void setMode(ModemMode mode, const ModemConfig& cfg = {});
    ModemMode mode() const { return m_modem->mode(); }
    QString modeName() const { return QString::fromStdString(m_modem->modeName()); }
    double passbandLowHz()  const { return m_modem->passbandLowHz(); }
    double passbandHighHz() const { return m_modem->passbandHighHz(); }

    // ── State ─────────────────────────────────────────────────────────────
    RxState rxState()        const;
    bool    dcdActive()      const { return m_dcdActive; }
    bool    isTransmitting()     const { return m_transmitting; }

    // ── RX gain ───────────────────────────────────────────────────────────
    // atomic: written from GUI-thread UI code, read from onAudioChunk() on
    // the worker thread once AudioEngine/DspPipeline move there (and, today,
    // from a GUI-thread level-meter lambda too) — see plan doc / DECISIONS.md.
    void  setRxGain(float linear) { m_rxGain.store(linear, std::memory_order_relaxed); }
    float rxGain()          const { return m_rxGain.load(std::memory_order_relaxed); }

    // ── AFC controls ──────────────────────────────────────────────────────
    void  setAfcEnabled(bool enabled) { m_modem->setAfcEnabled(enabled); }
    bool  afcEnabled()      const     { return m_modem->afcEnabled(); }
    float afcOffsetHz()     const     { return m_modem->afcOffsetHz(); }

    // ── Squelch (mode-specific meaning; 0.0 = off) ─────────────────────────
    void  setSquelchThreshold(float threshold) { m_modem->setSquelchThreshold(threshold); }
    float squelchThreshold() const             { return m_modem->squelchThreshold(); }

    bool lastTxWasCQ() const {
        return m_lastTxText.contains("CQ", Qt::CaseInsensitive);
    }

    // AFC range — beyond this AFC clamps and MainWindow warns operator.
    // Mirrors MfskModem::AFC_MAX_HZ; kept here too since MainWindow reads
    // it without a live modem instance available at UI-setup time. If a
    // future mode's AFC range differs, source this from IModem instead.
    static constexpr float AFC_MAX_HZ = 200.0f;

    // ── RS measurement cache ──────────────────────────────────────────────
    // Returns by value (not a pointer into m_rxCache) — updateRxCache()/
    // expireRxCache() write this from onAudioChunk() (worker thread once
    // AudioEngine/DspPipeline move there), while this is read directly from
    // the GUI thread on a callsign click. A pointer into the map wouldn't be
    // safe to use after the mutex below is released; a copied-out value is.
    std::optional<RxMeasurement> getRxMeasurement(const QString& callsign) const;
    static QString computeRS(const RxMeasurement& m);
    static QString parseSenderCallsign(const QString& text,
                                       const QString& myCallsign);

public slots:
    void onAudioChunk(const std::vector<float>& samples);
    void onTxComplete();

    // Generates steady tune-tone audio (mode-dependent; MFSK: 1000 Hz at
    // TX_AMPLITUDE) for adjusting TX level into the radio. Delivered via
    // tuneAudioReady() instead of a synchronous return value, since a
    // direct return can't be read once this runs on a different thread
    // than the caller. Does NOT set m_transmitting.
    void requestTuneAudio();

signals:
    void messageReceived(const HavenFSK::RxMessage& msg);
    // Continuous character-stream RX (PSK31) — one or a few decoded
    // characters at a time, not a discrete framed message. UI should
    // append this in place as flowing text, not as its own timestamped
    // row with CRC/FEC badges (those don't apply — see ModemRxEvent).
    void textCharacterReceived(const QString& text);
    void dcdChanged(bool active);
    void rxStateChanged(HavenFSK::RxState state);
    void txAudioReady(const std::vector<float>& samples);
    // Result of requestTuneAudio() — see its doc comment above.
    void tuneAudioReady(const std::vector<float>& samples);
    void preambleDetected(float score);
    void rxProgress(int symbolsReceived, int symbolsExpected);
    void afcOffsetChanged(float hz);
    void messageTransmitted(const QString& text);
    // Emitted instead of transmit()'s bool return on its two early-return
    // paths — callers should react to this signal rather than the return
    // value once transmit() is invoked via a queued cross-thread call
    // (QMetaObject::invokeMethod can't return a synchronous result).
    void transmitFailed(const QString& reason);
    void modeChanged(HavenFSK::ModemMode mode);
    // Fired from setMode() right after the m_modem swap — carries the new
    // mode's passband/name so callers don't need to read them back via
    // synchronous getters immediately after calling setMode() (unsafe once
    // setMode() runs on a different thread than the caller).
    void modeReady(HavenFSK::ModemMode mode, double passbandLowHz,
                   double passbandHighHz, const QString& modeName);

private:
    std::unique_ptr<IModem> m_modem;

    bool    m_dcdActive    = false;
    bool    m_transmitting = false;

    // ── RX gain ───────────────────────────────────────────────────────────
    std::atomic<float> m_rxGain {1.0f};

    // ── TX state ──────────────────────────────────────────────────────────
    QString m_lastTxText;

    // ── Change-tracking for polled IModem status (drives Qt signal emission
    //    at the same transition points the pre-refactor DspPipeline used) ──
    ModemRxState m_lastPolledRxState = ModemRxState::Idle;
    float        m_lastPolledAfcHz   = 0.0f;

    // ── RS measurement cache ──────────────────────────────────────────────
    // QMap only tolerates concurrent reads, not read-while-write — guarded
    // by m_rxCacheMutex since onAudioChunk() (worker thread) writes this
    // while the GUI thread reads it via getRxMeasurement() on a click.
    mutable std::mutex           m_rxCacheMutex;
    QMap<QString, RxMeasurement> m_rxCache;
    static constexpr int RS_CACHE_MINUTES = 10;

    void updateRxCache(const RxMessage& msg);
    void expireRxCache();

    void pollModemStatus();
};

} // namespace HavenFSK
