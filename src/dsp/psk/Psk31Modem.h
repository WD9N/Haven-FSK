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
    float        lastSnrDb() const override { return 0.0f; }  // not implemented yet

    // AFC: PSK31 tracks carrier drift continuously via the Costas loop
    // rather than a discrete per-frame frequency search, so "AFC enabled"
    // has no separate meaning here — the Costas loop always runs.
    void  setAfcEnabled(bool) override {}
    bool  afcEnabled()  const override { return true; }
    float afcOffsetHz() const override { return m_lastCarrierOffsetHz; }

    // Squelch: minimum average Costas-loop lock quality (0.0-1.0) a
    // decoded character's bits must meet to be surfaced to the UI.
    // Default 0.0 = off (every DCD-gated decode passes through) — real
    // over-the-air signals have frequency drift/phase noise/timing
    // jitter that legitimately lowers lock quality even on correctly
    // decoded characters, and an aggressive default silently broke RX
    // entirely on a real signal fldigi decoded fine. Off-by-default
    // until the operator has a working baseline to tune up from.
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
    bool         m_hadSignal = false;  // m_dcdActive on the previous chunk
    float        m_lastCarrierOffsetHz = 0.0f;

    // Squelch: running lock-quality average across the bits composing
    // the character currently being decoded (reset each time a
    // character boundary is reached — see processAudioChunk()).
    float m_qualitySum   = 0.0f;
    int   m_qualityCount = 0;
    float m_squelchThreshold = 0.0f;  // 0.0 = off; see setSquelchThreshold() doc

    // Simple RMS-threshold DCD — PSK31 has no preamble/sync-tone concept
    // to key off, unlike MFSK's carrier-detect band. Placeholder until
    // real-world tuning; see Psk31Demodulator::Result::lockQuality for a
    // more meaningful signal-present indicator once validated.
    static constexpr float DCD_RMS_THRESHOLD = 0.01f;
};

} // namespace HavenFSK
