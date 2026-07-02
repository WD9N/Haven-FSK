#pragma once
#include "IModem.h"
#include "Modulator.h"
#include "Demodulator.h"
#include "DCD.h"
#include "Preamble.h"
#include "Frame.h"
#include "MfskConstants.h"

// MfskModem — IModem implementation wrapping HAVEN-FSK's 16-tone MFSK
// stack. This hosts the RX state machine (preamble scan / frame collect),
// AFC, and diagnostic tooling that lived directly in DspPipeline before
// the modem abstraction (Phase 2) was introduced — relocated, not
// rewritten, so the tuned thresholds/behavior are unchanged.
//
// Note: unlike IModem.h itself, this implementation is not Qt-free — it
// uses QDebug for logging, matching the pre-refactor DspPipeline exactly.
// DspPipeline was already the Qt-facing exception within src/dsp/ before
// this refactor (QObject, QString, QDateTime); this preserves that,
// rather than rewriting every debug line during a relocation. New modes
// (e.g. Psk31Modem) should stay Qt-free per ADR-003 since they have no
// such legacy logging to preserve.

namespace HavenFSK {

class MfskModem : public IModem {
public:
    MfskModem();

    std::vector<float> modulateText(const std::string& text) override;
    std::vector<ModemRxEvent> processAudioChunk(
        const std::vector<float>& samples) override;
    void resetRx() override;
    void setRxSuspended(bool suspended) override { m_rxSuspended = suspended; }

    ModemRxState rxState()  const override { return m_rxState; }
    bool         dcdActive() const override { return m_dcdActive; }
    float        lastSnrDb() const override { return m_dcd.lastSnrDb(); }

    void  setAfcEnabled(bool enabled) override { m_afcEnabled = enabled; }
    bool  afcEnabled()  const override { return m_afcEnabled; }
    float afcOffsetHz() const override { return m_afcOffsetHz; }

    void setToneMonitor(bool active) override;
    bool toneMonitorActive() const override { return m_toneMonitorActive; }
    std::vector<float> generateDiagnosticAudio() const override;
    void runDiagnosticSelfTest() override;

    ModemMode   mode()     const override { return ModemMode::Mfsk16; }
    std::string modeName() const override { return "MFSK-16"; }
    double passbandLowHz()  const override { return BASE_FREQ; }
    double passbandHighHz() const override {
        return BASE_FREQ + NUM_TONES * SYMBOL_RATE;
    }

    // AFC range — beyond this AFC clamps and MainWindow warns operator.
    static constexpr float AFC_MAX_HZ = 200.0f;

private:
    // ── DSP objects ───────────────────────────────────────────────────────
    Modulator   m_modulator;
    Demodulator m_demodulator;
    DCD         m_dcd;
    Preamble    m_preamble;
    Frame       m_frame;

    // ── RX state ──────────────────────────────────────────────────────────
    ModemRxState m_rxState     = ModemRxState::Idle;
    bool         m_dcdActive   = false;
    bool         m_rxSuspended = false;  // true while this station is transmitting

    std::vector<float> m_rxBuffer;
    std::vector<float> m_preTrigger;

    static constexpr int MAX_BUFFER_SAMPLES = SAMPLE_RATE * 30;

    int m_timingOffset     = 0;
    int m_preambleSymOff   = -1;
    int m_nBlocks          = -1;
    int m_symsNeeded       = 0;
    int m_demodBinOffset   = 0;
    int m_scanTicks        = 0;
    int m_collectTicks     = 0;
    int m_lastCheckSamples = 0;

    static constexpr int SCAN_INTERVAL_CHUNKS = 8;
    static constexpr int COLLECT_TIMEOUT_CHUNKS =
        static_cast<int>(SAMPLE_RATE * 20.0 / AUDIO_CHUNK_SAMPLES);
    static constexpr float COARSE_PREAMBLE_THRESHOLD = 0.10f;
    static constexpr float SOFT_PREAMBLE_THRESHOLD    = 0.45f;
    static constexpr int   PRE_TRIGGER_SAMPLES = SAMPLE_RATE * 3;

    // ── Tone monitor ─────────────────────────────────────────────────────
    bool               m_toneMonitorActive = false;
    std::vector<float> m_monitorBuf;
    int                m_monitorSymCount   = 0;

    // ── AFC state ─────────────────────────────────────────────────────────
    float m_afcOffsetHz = 0.0f;
    bool  m_afcEnabled  = false;

    // ── Fine timing recovery (experimental, disabled by default) ───────────
    // Preamble detection already does an 8-step coarse timing sweep
    // (tryFindPreamble()) that sets m_timingOffset once. This adds an
    // additional, continuous nudge during Collecting, comparing decode
    // confidence at the current offset against +/- a small probe step and
    // drifting toward whichever wins — intended to track sample-clock
    // drift between TX/RX over a multi-second transmission. Kept off by
    // default (flip m_fineTimingEnabled to test) since it hasn't been
    // validated against real HF signals and the existing preamble-only
    // sweep is already known to work; this exists to be A/B tested, not
    // as a trusted default.
    bool m_fineTimingEnabled = false;
    int  m_timingOffsetBase  = 0;   // offset at preamble detection, for drift clamp
    int  m_fineTimingTick    = 0;
    static constexpr int FINE_TIMING_INTERVAL_CALLS = 4;               // throttle cost
    static constexpr int FINE_TIMING_PROBE_STEP     = SAMPLES_PER_SYMBOL / 16;
    static constexpr int FINE_TIMING_MAX_DRIFT       = SAMPLES_PER_SYMBOL / 4;

    float measureToneOffset(
        const std::vector<std::vector<float>>& softSymbols) const;

    // Average of (winning-tone energy / total energy) across softSymbols
    // — a simple decode-confidence metric used to compare candidate
    // timing offsets against each other (higher = cleaner alignment).
    float measureDecodeConfidence(
        const std::vector<std::vector<float>>& softSymbols) const;

    void applyFineTimingCorrection();

    // ── Helpers ───────────────────────────────────────────────────────────
    void tryFindPreamble(ModemRxEvent& outEvent);
    void tryCompleteFrame(std::vector<ModemRxEvent>& outEvents);
    void processFrame(const std::vector<std::vector<float>>& softSymbols,
                       ModemRxEvent& outEvent);
    void setRxState(ModemRxState newState);
};

} // namespace HavenFSK
