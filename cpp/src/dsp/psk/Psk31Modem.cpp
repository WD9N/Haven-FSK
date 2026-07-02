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
    return m_modulator.modulateText(text);
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

    for (float sample : samples) {
        auto symResult = m_demodulator.processSample(sample);
        if (!symResult.bitReady) continue;

        auto ch = m_varicode.decodeBit(symResult.bit);
        if (ch.has_value()) {
            ModemRxEvent ev;
            ev.hasMessage = true;
            ev.text       = std::string(1, *ch);
            // PSK31 has no CRC/FEC — crcOk/converged/fecIterations stay
            // at their ModemRxEvent defaults (false/false/0).
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
