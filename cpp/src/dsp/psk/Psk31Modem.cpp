#include "Psk31Modem.h"
#include <cmath>

namespace HavenFSK {

Psk31Modem::Psk31Modem(double baudRate, int variant)
    : m_baudRate(baudRate)
    , m_modulator(baudRate, PSK31_CARRIER_HZ)
    , m_demodulator(baudRate, PSK31_CARRIER_HZ)
{
    (void)variant;  // QPSK stretch goal — unimplemented, BPSK only for now
}

std::vector<float> Psk31Modem::modulateText(const std::string& text) {
    // Leading phase-reversal preamble (bit=0 repeated) so a receiving
    // station's Costas loop/AGC/timing recovery has something to lock
    // onto before real data arrives — see Psk31Constants.h for the
    // fldigi-matched symbol count and full rationale. The existing
    // Varicode decoder already treats a run of phase-reversal bits as
    // harmless idle (no RX-side change needed).
    std::vector<bool> bits(
        static_cast<size_t>(psk31PreambleSymbols(m_baudRate)), false);
    auto textBits = Varicode::encode(text);
    bits.insert(bits.end(), textBits.begin(), textBits.end());
    return m_modulator.modulateBits(bits);
}

std::vector<ModemRxEvent> Psk31Modem::processAudioChunk(
    const std::vector<float>& samples)
{
    std::vector<ModemRxEvent> events;

    // Placeholder DCD: RMS threshold over the chunk. PSK31 has no
    // preamble/carrier-detect band to key off like MFSK's DCD does;
    // this is a simple stand-in, not validated against real signals.
    double sumSq = 0.0;
    for (float s : samples) sumSq += static_cast<double>(s) * s;
    double rms = samples.empty() ? 0.0 : std::sqrt(sumSq / samples.size());
    m_dcdActive = rms >= DCD_RMS_THRESHOLD;
    m_rxState = m_dcdActive ? ModemRxState::Collecting : ModemRxState::Idle;

    // Squelch, stage 1: no carrier present at all -> don't bother
    // demodulating this chunk. Feeding pure noise through the Costas
    // loop/Varicode decoder is exactly what was producing a constant
    // stream of garbage characters between real transmissions. Reset
    // decode state on the falling edge so a genuine signal starts clean
    // rather than resuming mid-noise-corrupted state.
    if (!m_dcdActive) {
        if (m_hadSignal) {
            m_demodulator.reset();
            m_varicode.reset();
            m_qualitySum = 0.0f;
            m_qualityCount = 0;
        }
        m_hadSignal = false;
        return events;
    }
    m_hadSignal = true;

    for (float sample : samples) {
        auto symResult = m_demodulator.processSample(sample);
        if (!symResult.bitReady) continue;

        m_qualitySum += symResult.lockQuality;
        ++m_qualityCount;

        auto ch = m_varicode.decodeBit(symResult.bit);
        if (ch.has_value()) {
            // Squelch, stage 2: average lock quality across the bits that
            // made up this character. Even with DCD gating there, a weak
            // or marginal signal can still decode-but-be-garbage; this
            // catches that case per-character rather than all-or-nothing.
            float avgQuality = (m_qualityCount > 0)
                ? m_qualitySum / static_cast<float>(m_qualityCount) : 0.0f;
            m_qualitySum = 0.0f;
            m_qualityCount = 0;

            if (avgQuality < m_squelchThreshold) continue;

            ModemRxEvent ev;
            ev.hasMessage      = true;
            ev.text            = std::string(1, *ch);
            ev.isFramedMessage = false;  // continuous stream, not a discrete message
            // PSK31 has no CRC/FEC — crcOk/converged/fecIterations stay
            // at their ModemRxEvent defaults (false/false/0), and are
            // ignored downstream since isFramedMessage is false.
            events.push_back(ev);
        }
    }

    return events;
}

void Psk31Modem::resetRx() {
    m_demodulator.reset();
    m_varicode.reset();
    m_rxState = ModemRxState::Idle;
}

} // namespace HavenFSK
