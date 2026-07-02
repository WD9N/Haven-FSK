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

    // Simple RMS-threshold DCD — PSK31 has no preamble/sync-tone concept
    // to key off, unlike MFSK's carrier-detect band. Placeholder until
    // real-world tuning; see Psk31Demodulator::Result::lockQuality for a
    // more meaningful signal-present indicator once validated.
    static constexpr float DCD_RMS_THRESHOLD = 0.01f;
};

} // namespace HavenFSK
