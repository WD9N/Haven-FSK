#pragma once
#include <QObject>
#include <QString>
#include <QMap>
#include <QDateTime>
#include <vector>
#include <memory>
#include "Constants.h"
#include "IModem.h"

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
    bool    toneMonitorActive()  const { return m_modem->toneMonitorActive(); }

    // ── RX gain ───────────────────────────────────────────────────────────
    void  setRxGain(float linear) { m_rxGain = linear; }
    float rxGain()          const { return m_rxGain; }

    // ── AFC controls ──────────────────────────────────────────────────────
    void  setAfcEnabled(bool enabled) { m_modem->setAfcEnabled(enabled); }
    bool  afcEnabled()      const     { return m_modem->afcEnabled(); }
    float afcOffsetHz()     const     { return m_modem->afcOffsetHz(); }

    bool lastTxWasCQ() const {
        return m_lastTxText.contains("CQ", Qt::CaseInsensitive);
    }

    // AFC range — beyond this AFC clamps and MainWindow warns operator.
    // Mirrors MfskModem::AFC_MAX_HZ; kept here too since MainWindow reads
    // it without a live modem instance available at UI-setup time. If a
    // future mode's AFC range differs, source this from IModem instead.
    static constexpr float AFC_MAX_HZ = 200.0f;

    // ── RS measurement cache ──────────────────────────────────────────────
    const RxMeasurement* getRxMeasurement(const QString& callsign) const;
    static QString computeRS(const RxMeasurement& m);
    static QString parseSenderCallsign(const QString& text,
                                       const QString& myCallsign);

public slots:
    void onAudioChunk(const std::vector<float>& samples);
    void onTxComplete();

    // Internal demodulator diagnostic — no radio required.
    void runToneSweepTest();

    // Enable/disable continuous per-symbol tone logging on received audio.
    void setToneMonitor(bool active);

    // Generates diagnostic audio for RF testing (mode-dependent content;
    // MFSK: 16-tone x 500ms sweep). Does NOT set m_transmitting — the RX
    // pipeline and tone monitor stay active so the received signal can be
    // logged while TX is in progress.
    std::vector<float> generateToneSweepAudio() const;

signals:
    void messageReceived(const HavenFSK::RxMessage& msg);
    void dcdChanged(bool active);
    void rxStateChanged(HavenFSK::RxState state);
    void txAudioReady(const std::vector<float>& samples);
    void preambleDetected(float score);
    void rxProgress(int symbolsReceived, int symbolsExpected);
    void afcOffsetChanged(float hz);
    void messageTransmitted(const QString& text);
    void modeChanged(HavenFSK::ModemMode mode);

private:
    std::unique_ptr<IModem> m_modem;

    bool    m_dcdActive    = false;
    bool    m_transmitting = false;

    // ── RX gain ───────────────────────────────────────────────────────────
    float m_rxGain {1.0f};

    // ── TX state ──────────────────────────────────────────────────────────
    QString m_lastTxText;

    // ── Change-tracking for polled IModem status (drives Qt signal emission
    //    at the same transition points the pre-refactor DspPipeline used) ──
    ModemRxState m_lastPolledRxState = ModemRxState::Idle;
    float        m_lastPolledAfcHz   = 0.0f;

    // ── RS measurement cache ──────────────────────────────────────────────
    QMap<QString, RxMeasurement> m_rxCache;
    static constexpr int RS_CACHE_MINUTES = 10;

    void updateRxCache(const RxMessage& msg);
    void expireRxCache();

    void pollModemStatus();
};

} // namespace HavenFSK
