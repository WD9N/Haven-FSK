#pragma once
#include "../IModem.h"
#include "Psk31Modulator.h"
#include "Psk31Demodulator.h"
#include "Varicode.h"
#include "Psk31Constants.h"

// Psk31Modem — IModem implementation for BPSK31/63/125. Qt-free
// (std:: only, per ADR-003) — unlike MfskModem, this is new code with no
// legacy Qt-based logging to preserve, so it follows the strict DSP-layer
// rule from the start.

namespace HavenFSK {

class Psk31Modem : public IModem {
public:
    explicit Psk31Modem(double baudRate = PSK31_DEFAULT_BAUD,
                         int variant = 0 /* 0=BPSK; QPSK is a stretch goal, unimplemented */);

    std::vector<float> modulateText(const std::string& text) override;
    std::vector<ModemRxEvent> processAudioChunk(
        const std::vector<float>& samples) override;
    void resetRx() override;

    ModemRxState rxState()  const override { return m_rxState; }
    bool         dcdActive() const override { return m_dcdActive; }
    // In-band vs adjacent-band ratio from the narrowband DCD. Not the
    // 2500 Hz-referenced SNR convention MFSK reports — useful as a
    // relative signal-quality indicator, not comparable across modes.
    float        lastSnrDb() const override { return m_dcdSnrDb; }

    // AFC: PSK31 tracks carrier drift continuously via the Costas loop
    // rather than a discrete per-frame frequency search, so "AFC enabled"
    // has no separate meaning here — the Costas loop always runs.
    void  setAfcEnabled(bool) override {}
    bool  afcEnabled()  const override { return true; }
    float afcOffsetHz() const override { return m_lastCarrierOffsetHz; }
    void  nudgeCarrierHz(float deltaHz) override {
        m_demodulator.nudgeCarrierHz(deltaHz);
        m_lastCarrierOffsetHz = m_demodulator.carrierOffsetHz();
    }

    // Squelch: minimum average lock quality (0.0-1.0) a decoded
    // character's bits must meet to be surfaced to the UI. The metric is
    // the doubled-differential-phase coherence (see Psk31Demodulator):
    // ~1.0 on a locked signal — even a weak one — and random-walking
    // near ~0.2 on noise, so unlike the earlier per-bit |dI|/mag metric
    // (which sat at ~0.64 on pure noise) a real threshold exists.
    // Default 0.5: passes locked signals with margin while gating the
    // stray bits emitted right after timing acquisition and during the
    // DCD hang tail, where coherence is genuinely low.
    void  setSquelchThreshold(float threshold) override {
        m_squelchThreshold = threshold;
    }
    float squelchThreshold() const override { return m_squelchThreshold; }

    ModemMode   mode()     const override { return ModemMode::Psk31; }
    std::string modeName() const override { return "PSK31"; }
    double passbandLowHz()  const override {
        return PSK31_CARRIER_HZ - psk31PassbandHalfWidthHz(m_baudRate);
    }
    double passbandHighHz() const override {
        return PSK31_CARRIER_HZ + psk31PassbandHalfWidthHz(m_baudRate);
    }

private:
    double m_baudRate;
    Psk31Modulator   m_modulator;
    Psk31Demodulator m_demodulator;
    Varicode         m_varicode;

    ModemRxState m_rxState   = ModemRxState::Idle;
    bool         m_dcdActive = false;
    float        m_lastCarrierOffsetHz = 0.0f;

    // Squelch: running lock-quality average across the bits composing
    // the character currently being decoded (reset each time a
    // character boundary is reached — see processAudioChunk()).
    float m_qualitySum   = 0.0f;
    int   m_qualityCount = 0;
    float m_squelchThreshold = 0.5f;  // see setSquelchThreshold() doc

    // ── Narrowband DCD ──────────────────────────────────────────────
    // In-band power (carrier +- 1.5x baud, via Goertzel bins) against
    // two adjacent noise-reference bands, smoothed across chunks, with
    // hysteresis and a hang time. Replaces the original broadband-RMS
    // placeholder, which keyed open on band noise alone (a constant
    // ~145 garbage chars/min on pure noise in the 2026-07-13 baseline
    // bench) and flapped on weak signals, resetting decode state
    // mid-transmission. Ratio thresholds are audio-level independent —
    // both bands scale together with RX gain.
    //
    // Threshold math: in-band spans ~3x baud ≈ 94 Hz for PSK31. Noise
    // alone puts the ratio near 0 dB; a signal at -10 dB SNR (2500 Hz
    // ref) concentrates its full power into that 94 Hz, lifting the
    // ratio to ~+5.6 dB, and -5 dB SNR gives ~+9.7 dB. ON at +5 dB
    // therefore admits signals down to roughly the -10 dB mark while
    // sitting ~5 sigma-smoothed dB above the noise-only resting point.
    bool  updateDcd(const std::vector<float>& samples);
    static double goertzelPower(const std::vector<float>& x, double freqHz);

    float m_dcdSnrDb    = 0.0f;  // smoothed in-band/out-band ratio, dB
    int   m_dcdHangLeft = 0;     // chunks remaining before drop takes effect

    static constexpr float DCD_ON_DB       = 5.0f;
    static constexpr float DCD_OFF_DB      = 2.5f;
    static constexpr float DCD_SMOOTH_ALPHA = 0.3f;  // per-chunk IIR
    static constexpr int   DCD_HANG_CHUNKS = 5;      // ~213 ms at 2048/48k
};

} // namespace HavenFSK
